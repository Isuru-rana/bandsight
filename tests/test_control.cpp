// SPDX-License-Identifier: GPL-3.0-or-later
// The control socket, end to end over a real Unix socket. Every client is a
// local user in group bandsight, the daemon is root, and one misbehaving
// client must not be able to stop the collector.
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "check.h"
#include "json_frame.h"
#include "unix_socket_control.h"

using namespace bandsight;

namespace {

std::string g_path;

int connect_client() {
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    sockaddr_un a{};
    a.sun_family = AF_UNIX;
    std::snprintf(a.sun_path, sizeof(a.sun_path), "%s", g_path.c_str());
    if (::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

void send_all(int fd, const std::string& s) {
    std::size_t off = 0;
    while (off < s.size()) {
        const ssize_t n = ::send(fd, s.data() + off, s.size() - off, MSG_NOSIGNAL);
        if (n <= 0) return;
        off += std::size_t(n);
    }
}

// Reads one frame body, polling the server between attempts. "" on EOF or
// timeout; "<eof>" when the server closed the connection.
std::map<int, std::string> g_pending;   // bytes read past the last frame, per fd

std::string read_frame(UnixSocketControl& srv, int fd) {
    std::string& buf = g_pending[fd];
    for (int i = 0; i < 200; ++i) {
        if (buf.size() >= 4) {
            const auto* b = reinterpret_cast<const unsigned char*>(buf.data());
            const std::size_t len = b[0] | b[1] << 8 | b[2] << 16 | std::size_t(b[3]) << 24;
            if (buf.size() >= 4 + len) {
                std::string body = buf.substr(4, len);
                buf.erase(0, 4 + len);
                return body;
            }
        }
        srv.poll(5);
        pollfd p{fd, POLLIN, 0};
        if (::poll(&p, 1, 5) <= 0) continue;
        char tmp[4096];
        const ssize_t n = ::recv(fd, tmp, sizeof(tmp), 0);
        if (n <= 0) return "<eof>";
        buf.append(tmp, std::size_t(n));
    }
    return "";
}

void forget(int fd) {
    g_pending.erase(fd);
    ::close(fd);
}

bool closed_by_server(UnixSocketControl& srv, int fd) {
    for (int i = 0; i < 200; ++i) {
        srv.poll(5);
        pollfd p{fd, POLLIN, 0};
        if (::poll(&p, 1, 5) <= 0) continue;
        char tmp[65536];
        const ssize_t n = ::recv(fd, tmp, sizeof(tmp), MSG_DONTWAIT);
        if (n == 0 || (n < 0 && errno != EAGAIN)) return true;
    }
    return false;
}

void conversation() {
    UnixSocketControl srv(g_path);
    std::vector<std::pair<std::string, std::string>> writes;
    srv.set_config_handler([&](const std::string& k, const std::string& v) {
        writes.emplace_back(k, v);
        return k == "cap_bytes" ? std::string() : std::string("unknown config key: " + k);
    });
    CHECK(srv.start());
    struct stat st{};
    CHECK(::stat(g_path.c_str(), &st) == 0 && (st.st_mode & 0777) == 0660);
    srv.set_hello(frame(R"({"t":"hello"})"));

    const int c = connect_client();
    CHECK(c >= 0);
    CHECK(read_frame(srv, c) == R"({"t":"hello"})");

    send_all(c, "{\"cmd\":\"ping\"}\n");
    CHECK(read_frame(srv, c) == encode_pong());

    send_all(c, "{\"cmd\":\"set_config\",\"key\":\"cap_bytes\",\"value\":\"100\"}\n");
    CHECK(read_frame(srv, c) == encode_ok());
    send_all(c, "{\"cmd\":\"set_config\",\"key\":\"schema_version\",\"value\":\"9\"}\n");
    CHECK(read_frame(srv, c).find("unknown config key") != std::string::npos);
    CHECK(writes.size() == 2 && writes[0].first == "cap_bytes" && writes[0].second == "100");

    // Two commands in one write, and one split across two writes.
    send_all(c, "{\"cmd\":\"ping\"}\n{\"cmd\":\"ping\"}\n{\"cmd\":");
    CHECK(read_frame(srv, c) == encode_pong());
    CHECK(read_frame(srv, c) == encode_pong());
    send_all(c, "\"ping\"}\n");
    CHECK(read_frame(srv, c) == encode_pong());

    // Broadcast reaches the client.
    srv.broadcast(frame("{\"t\":\"sample\"}"));
    CHECK(read_frame(srv, c) == "{\"t\":\"sample\"}");
    forget(c);

    // A command followed at once by a half-close is still answered.
    const int h = connect_client();
    CHECK(read_frame(srv, h) == R"({"t":"hello"})");
    send_all(h, "{\"cmd\":\"ping\"}\n");
    ::shutdown(h, SHUT_WR);
    CHECK(read_frame(srv, h) == encode_pong());
    CHECK(closed_by_server(srv, h));
    forget(h);
}

// A client that never sends a newline is dropped, not buffered forever.
void no_newline_dropped() {
    UnixSocketControl srv(g_path);
    CHECK(srv.start());
    const int c = connect_client();
    ::fcntl(c, F_SETFL, O_NONBLOCK);
    const std::string junk(16 * 1024, 'x');
    bool dropped = false;
    for (int i = 0; i < 40 && !dropped; ++i) {
        ::send(c, junk.data(), junk.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
        srv.poll(5);
        char tmp[16];
        const ssize_t n = ::recv(c, tmp, sizeof(tmp), MSG_DONTWAIT);
        dropped = n == 0 || (n < 0 && errno != EAGAIN);
    }
    CHECK(dropped);
    ::close(c);
}

// At most 64 clients; the 65th is closed, and a slot frees on disconnect.
void client_cap() {
    UnixSocketControl srv(g_path);
    CHECK(srv.start());
    srv.set_hello(frame("{}"));
    std::vector<int> fds;
    for (int i = 0; i < 64; ++i) {
        fds.push_back(connect_client());
        CHECK(read_frame(srv, fds.back()) == "{}");
    }
    const int extra = connect_client();
    CHECK(closed_by_server(srv, extra));
    forget(extra);

    forget(fds.back());
    fds.pop_back();
    for (int i = 0; i < 10; ++i) srv.poll(5);
    const int again = connect_client();
    CHECK(read_frame(srv, again) == "{}");
    forget(again);
    for (int fd : fds) forget(fd);
}

// A second instance must neither take over nor delete a live socket.
void second_instance_refused() {
    UnixSocketControl first(g_path);
    CHECK(first.start());
    {
        UnixSocketControl second(g_path);
        CHECK(!second.start());
    }   // its destructor must leave the path alone
    const int c = connect_client();
    CHECK(c >= 0);
    if (c >= 0) ::close(c);
}

}  // namespace

int main() {
    char tmpl[] = "/tmp/bandsight-test-control-XXXXXX";
    const char* dir = ::mkdtemp(tmpl);
    CHECK(dir != nullptr);
    if (!dir) return 1;
    g_path = std::string(dir) + "/sock";

    conversation();
    no_newline_dropped();
    client_cap();
    second_instance_refused();
    // Each instance removed the socket it bound.
    struct stat st{};
    CHECK(::stat(g_path.c_str(), &st) != 0);
    ::rmdir(dir);

    if (g_failures) std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    else std::printf("test_control: all checks passed\n");
    return g_failures ? 1 : 0;
}
