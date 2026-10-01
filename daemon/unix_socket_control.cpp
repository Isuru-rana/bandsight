// SPDX-License-Identifier: GPL-3.0-or-later
#include "unix_socket_control.h"

#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

#include "json_frame.h"

namespace bandsight {

namespace {
// Generous next to a real command (tens of bytes) and small next to the unit's
// MemoryMax=128M.
constexpr std::size_t kMaxCommandBytes = 64 * 1024;
}  // namespace

UnixSocketControl::UnixSocketControl(std::string socket_path)
    : path_(std::move(socket_path)) {}

UnixSocketControl::~UnixSocketControl() {
    for (auto& [fd, _] : clients_) ::close(fd);
    if (listen_fd_ >= 0) ::close(listen_fd_);
    if (epoll_fd_ >= 0) ::close(epoll_fd_);
    // Only if THIS instance bound it. Unlinking unconditionally meant a second
    // daemon that failed to start - the common "it is already running" case -
    // deleted the running instance's socket on its way out, after which every
    // GUI reconnect failed against a path with nothing behind it.
    if (bound_ && !path_.empty()) ::unlink(path_.c_str());
}

bool UnixSocketControl::start() {
    listen_fd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (listen_fd_ < 0) {
        std::fprintf(stderr, "bandsightd: control socket() failed: %s\n", strerror(errno));
        return false;
    }

    // A stale socket from a previous run must be removed, but a LIVE one must
    // not: unlinking unconditionally meant a second daemon did not fail to
    // start, it took the path from the running one, which then held a socket
    // with no name while every new client reached the impostor. Probe first -
    // if something accepts a connection there, this path belongs to it.
    {
        struct sockaddr_un probe_addr{};
        probe_addr.sun_family = AF_UNIX;
        std::strncpy(probe_addr.sun_path, path_.c_str(), sizeof(probe_addr.sun_path) - 1);
        const int probe = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (probe >= 0) {
            const bool live = ::connect(probe, reinterpret_cast<struct sockaddr*>(&probe_addr),
                                        sizeof(probe_addr)) == 0;
            ::close(probe);
            if (live) {
                std::fprintf(stderr,
                             "bandsightd: %s is already served by a running instance; "
                             "refusing to take it over\n", path_.c_str());
                return false;
            }
        }
        ::unlink(path_.c_str());   // nothing answered: stale, ours to clear
    }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path_.c_str(), sizeof(addr.sun_path) - 1);
    if (::bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::fprintf(stderr, "bandsightd: control bind(%s) failed: %s\n", path_.c_str(),
                     strerror(errno));
        return false;
    }
    bound_ = true;

    // bind() yields 0777 & ~umask, which is 0770 under UMask=0007. Set the mode
    // explicitly so it does not depend on the unit's umask. chmod on the path,
    // not fchmod on the fd: the filesystem entry is a separate inode.
    if (::chmod(path_.c_str(), 0660) != 0) {
        std::fprintf(stderr, "bandsightd: chmod(%s, 0660) failed: %s\n", path_.c_str(),
                     strerror(errno));
        return false;
    }

    if (::listen(listen_fd_, 8) < 0) {
        std::fprintf(stderr, "bandsightd: control listen() failed: %s\n", strerror(errno));
        return false;
    }

    epoll_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0) {
        std::fprintf(stderr, "bandsightd: control epoll_create1() failed: %s\n",
                     strerror(errno));
        // Without this, the socket would keep existing and accepting()able (the
        // bind()/listen() above already succeeded) while poll() never services
        // it - a client connects successfully and then hangs forever. Tear the
        // listening socket down instead so the failure is visible immediately.
        ::close(listen_fd_);
        listen_fd_ = -1;
        ::unlink(path_.c_str());
        return false;
    }
    struct epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = listen_fd_;
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &ev) != 0) {
        // Nothing would ever be readable, so the socket would exist and accept
        // nothing - a failure that otherwise presents as "the GUI never
        // connects" with no message anywhere.
        std::fprintf(stderr, "bandsightd: control epoll_ctl(listen) failed: %s\n",
                     strerror(errno));
        return false;
    }

    std::fprintf(stderr, "bandsightd: control socket listening at %s\n", path_.c_str());
    return true;
}

void UnixSocketControl::set_hello(std::string framed_hello) {
    hello_ = std::move(framed_hello);
}

void UnixSocketControl::send_framed(int fd, const std::string& framed) {
    // MSG_NOSIGNAL: a client that disappears must not kill the daemon.
    const ssize_t n = ::send(fd, framed.data(), framed.size(), MSG_NOSIGNAL);
    if (n < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) drop(fd);
        return;
    }
    if (static_cast<std::size_t>(n) != framed.size()) {
        // A non-blocking send() on a stream socket can return a partial count
        // whenever some but not all of the payload fits in the send buffer -
        // this depends on free buffer space, not frame size, so it is reachable
        // any time a client stalls (e.g. blocked on a slow query) long enough
        // for its receive buffer to back up. The client's length-prefixed
        // stream is now misaligned and cannot be resynchronised from here.
        // Dropping it is honest - it reconnects and gets a fresh hello. A
        // per-client outbuf with EPOLLOUT is the proper fix, later.
        drop(fd);
    }
}

void UnixSocketControl::broadcast(const std::string& framed) {
    std::vector<int> fds;
    fds.reserve(clients_.size());
    for (const auto& [fd, _] : clients_) fds.push_back(fd);
    for (int fd : fds) send_framed(fd, framed);
}

void UnixSocketControl::broadcast_to(const std::string& topic, const std::string& framed) {
    std::vector<int> fds;
    for (const auto& [fd, c] : clients_) {
        if (c.topics.count(topic)) fds.push_back(fd);
    }
    for (int fd : fds) send_framed(fd, framed);
}

void UnixSocketControl::accept_clients() {
    for (;;) {
        const int fd = ::accept4(listen_fd_, nullptr, nullptr,
                                 SOCK_CLOEXEC | SOCK_NONBLOCK);
        if (fd < 0) {
            // Only EAGAIN means the queue is empty. Treating every errno that
            // way turned EMFILE into a spin: the connection stays queued, the
            // level-triggered epoll reports it readable again immediately, and
            // the daemon burns a core doing nothing until a descriptor frees up.
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            if (errno == EINTR || errno == ECONNABORTED) continue;   // retryable
            std::fprintf(stderr, "bandsightd: accept failed: %s\n", strerror(errno));
            return;
        }

        // Diagnostics only. Authorization already happened: the kernel checked
        // write permission on the socket path at connect(), against the caller's
        // full credential set including supplementary groups.
        struct ucred cred{};
        socklen_t len = sizeof(cred);
        if (::getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) == 0) {
            std::fprintf(stderr, "bandsightd: client connected pid=%d uid=%d\n",
                         cred.pid, cred.uid);
        }

        clients_[fd] = Client{};
        struct epoll_event ev{};
        ev.events = EPOLLIN | EPOLLRDHUP;
        ev.data.fd = fd;
        if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) != 0) {
            // An accepted client nobody will ever poll is worse than a refused
            // one: it sits connected, silent, waiting for a hello that never
            // comes.
            std::fprintf(stderr, "bandsightd: control epoll_ctl(client) failed: %s\n",
                         strerror(errno));
            clients_.erase(fd);
            ::close(fd);
            continue;
        }

        if (!hello_.empty()) send_framed(fd, hello_);
    }
}

void UnixSocketControl::handle_command(int fd, const std::string& line) {
    auto it = clients_.find(fd);
    if (it == clients_.end()) return;

    // Commands are small and few; substring matching avoids a JSON parser in the
    // daemon. Add a parser here if the command set grows beyond a handful.
    if (line.find("\"cmd\":\"ping\"") != std::string::npos) {
        send_framed(fd, frame(encode_pong()));
    } else if (line.find("\"cmd\":\"sub\"") != std::string::npos) {
        if (line.find("\"what\":\"conns\"") != std::string::npos) {
            // Recorded so the seam still works once the socket-cookie map
            // lands, but nothing calls broadcast_to("conns", ...) yet - tell
            // the client explicitly rather than leaving it subscribed in
            // silence, indistinguishable from "no connections right now".
            // it->second is not touched again in this function after
            // send_framed(), which may call drop(fd) and erase this entry.
            it->second.topics.insert("conns");
            send_framed(fd, frame(encode_error("conns not implemented")));
        }
    } else if (line.find("\"cmd\":\"set_config\"") != std::string::npos) {
        // The one inbound command that changes daemon state (spec §7). Every
        // decision about WHETHER it may is DaemonLoop's, through the handler -
        // this layer only reads the two fields and reports the answer.
        const std::string key = json_string_field(line, "key");
        const std::string value = json_string_field(line, "value");
        std::string err = config_handler_ ? config_handler_(key, value)
                                          : std::string("set_config unavailable");
        send_framed(fd, frame(err.empty() ? encode_ok() : encode_error(err)));
    } else if (line.find("\"cmd\":\"unsub\"") != std::string::npos) {
        if (line.find("\"what\":\"conns\"") != std::string::npos) {
            it->second.topics.erase("conns");
        }
    }
}

void UnixSocketControl::handle_readable(int fd) {
    char buf[2048];
    for (;;) {
        // Re-looked-up on every pass, not held across handle_command(): a ping
        // reply that fails to send drops the client from within handle_command,
        // which would otherwise leave this loop holding a dangling iterator
        // into an erased unordered_map bucket.
        auto it = clients_.find(fd);
        if (it == clients_.end()) return;

        const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n > 0) {
            it->second.inbuf.append(buf, static_cast<std::size_t>(n));
            // A client that never sends a newline would otherwise grow this
            // without limit inside a daemon with MemoryMax=128M, so a peer that
            // can reach the socket can end the collector. Commands are tens of
            // bytes; anything past the cap is not a command being assembled.
            if (it->second.inbuf.size() > kMaxCommandBytes) {
                std::fprintf(stderr,
                             "bandsightd: client sent %zu bytes with no newline; "
                             "dropping it\n", it->second.inbuf.size());
                drop(fd);
                return;
            }
            // Commands are newline-delimited; frames only flow outbound.
            std::size_t nl;
            while ((nl = it->second.inbuf.find('\n')) != std::string::npos) {
                // Consume the line before dispatching: handle_command may call
                // send_framed -> drop(fd), which erases `it`. Nothing below this
                // point may touch it->second again.
                std::string line = it->second.inbuf.substr(0, nl);
                it->second.inbuf.erase(0, nl + 1);
                handle_command(fd, line);
                it = clients_.find(fd);
                if (it == clients_.end()) return;
            }
            continue;
        }
        if (n == 0) { drop(fd); return; }
        if (errno == EAGAIN || errno == EWOULDBLOCK) return;
        drop(fd);
        return;
    }
}

void UnixSocketControl::drop(int fd) {
    ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    ::close(fd);
    clients_.erase(fd);
}

void UnixSocketControl::poll(int timeout_ms) {
    if (epoll_fd_ < 0) {
        // start() never got a usable epoll fd (socket()/bind()/chmod()/listen()
        // or epoll_create1() failed). Returning instantly here - as opposed to
        // blocking like a real epoll_wait() would - is what turned "no control
        // socket" into an unthrottled, 100%-CPU tick loop (see C3): the caller's
        // pacing depends on this call actually taking timeout_ms. Sleep instead
        // so accounting still degrades to interface-only, at 1 Hz, rather than
        // as fast as the CPU allows.
        if (timeout_ms > 0) {
            struct timespec ts{timeout_ms / 1000, (timeout_ms % 1000) * 1000000L};
            ::nanosleep(&ts, nullptr);
        }
        return;
    }
    struct epoll_event events[16];
    const int n = ::epoll_wait(epoll_fd_, events, 16, timeout_ms);
    for (int i = 0; i < n; ++i) {
        const int fd = events[i].data.fd;
        if (fd == listen_fd_) {
            accept_clients();
        } else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
            drop(fd);
        } else {
            handle_readable(fd);
        }
    }
}

}  // namespace bandsight
