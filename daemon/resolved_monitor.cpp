// SPDX-License-Identifier: GPL-3.0-or-later
#include "resolved_monitor.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>

namespace bandsight {

namespace {

constexpr std::int64_t kRetrySeconds = 30;
// A peer that never ends a message must not grow the buffer without bound.
constexpr std::size_t kMaxBuffer = 4 * 1024 * 1024;
// Cleared rather than evicted when full: names already written to the
// database keep them, and a live lookup re-learns anything still in use.
constexpr std::size_t kMaxNames = 65536;

const char kSubscribe[] = R"({"method":"io.systemd.Resolve.Monitor.SubscribeQueryResults","more":true})";
const char kDumpCache[] = R"({"method":"io.systemd.Resolve.Monitor.DumpCache"})";

// Connects without blocking and sends one call, NUL-terminated as varlink
// frames every message. -1 with errno set on failure.
int open_call(const std::string& path, const char* call) {
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    sockaddr_un a{};
    a.sun_family = AF_UNIX;
    std::strncpy(a.sun_path, path.c_str(), sizeof(a.sun_path) - 1);
    const std::size_t len = std::strlen(call) + 1;   // with the NUL
    if (::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0 ||
        ::send(fd, call, len, MSG_NOSIGNAL) != static_cast<ssize_t>(len)) {
        const int e = errno;
        ::close(fd);
        errno = e;
        return -1;
    }
    return fd;
}

}  // namespace

ResolvedMonitor::ResolvedMonitor(std::string socket_path, std::size_t max_read_per_poll)
    : path_(std::move(socket_path)), max_read_(max_read_per_poll) {}

ResolvedMonitor::~ResolvedMonitor() {
    close(sub_);
    close(dump_);
}

const std::string& ResolvedMonitor::name_for(const std::string& ip) const {
    static const std::string kNone;
    const auto it = names_.find(ip);
    return it == names_.end() ? kNone : it->second;
}

void ResolvedMonitor::close(Conn& c) {
    if (c.fd >= 0) ::close(c.fd);
    c.fd = -1;
    c.buf.clear();
}

void ResolvedMonitor::outage(std::int64_t now, const std::string& reason) {
    close(sub_);
    close(dump_);
    retry_at_ = now + kRetrySeconds;
    if (!logged_outage_)
        std::fprintf(stderr, "bandsightd: host names unavailable: %s\n", reason.c_str());
    logged_outage_ = true;
}

void ResolvedMonitor::remember(const IpName& p) {
    if (names_.size() >= kMaxNames && !names_.count(p.ip)) names_.clear();
    names_[p.ip] = p.name;
}

void ResolvedMonitor::connect_now(std::int64_t now) {
    sub_.fd = open_call(path_, kSubscribe);
    if (sub_.fd < 0) {
        outage(now, path_ + ": " + std::strerror(errno));
        return;
    }
    // The dump is a bonus: if it fails, the live feed still names new lookups.
    dump_.fd = open_call(path_, kDumpCache);
    if (logged_outage_)
        std::fprintf(stderr, "bandsightd: host names available again\n");
    logged_outage_ = false;
}

bool ResolvedMonitor::drain(Conn& c, std::vector<IpName>& out, std::string& reason,
                            bool& got_reply) {
    char chunk[16384];
    for (std::size_t budget = max_read_; budget > 0;) {
        const ssize_t n = ::read(c.fd, chunk, std::min(sizeof(chunk), budget));
        if (n > 0) {
            budget -= std::size_t(n);
            c.buf.append(chunk, std::size_t(n));
            std::size_t start = 0, nul;
            while ((nul = c.buf.find('\0', start)) != std::string::npos) {
                const std::string_view msg(c.buf.data() + start, nul - start);
                start = nul + 1;
                got_reply = true;
                if (is_varlink_error(msg)) {
                    reason = "resolved refused: " + std::string(msg);
                    return false;
                }
                for (auto& p : names_from_reply(msg)) out.push_back(std::move(p));
            }
            c.buf.erase(0, start);
            if (c.buf.size() > kMaxBuffer) {
                reason = "reply larger than 4 MiB";
                return false;
            }
            continue;
        }
        if (n == 0) {
            reason = "resolved closed the connection";
            return false;
        }
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
        reason = std::strerror(errno);
        return false;
    }
    return true;   // budget spent; the rest waits in the socket
}

std::vector<IpName> ResolvedMonitor::poll(std::int64_t now) {
    std::vector<IpName> out;
    if (sub_.fd < 0) {
        if (now < retry_at_) return out;
        connect_now(now);
        if (sub_.fd < 0) return out;
    }

    std::string reason;
    if (dump_.fd >= 0) {
        bool got = false;
        // One reply and done; a failed dump costs only the backfill.
        if (!drain(dump_, out, reason, got) || got) close(dump_);
    }
    bool got = false;
    if (!drain(sub_, out, reason, got)) outage(now, reason);

    for (const auto& p : out) remember(p);
    return out;
}

}  // namespace bandsight
