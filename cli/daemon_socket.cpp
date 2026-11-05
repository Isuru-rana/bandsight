// SPDX-License-Identifier: GPL-3.0-or-later
#include "daemon_socket.h"

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>

namespace bandsight::cli {

namespace {
using Clock = std::chrono::steady_clock;

int remaining_ms(Clock::time_point deadline) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
    return left.count() > 0 ? int(left.count()) : 0;
}
}  // namespace

DaemonSocket::~DaemonSocket() {
    if (fd_ >= 0) ::close(fd_);
}

DaemonSocket::State DaemonSocket::connect(const std::string& path, int timeout_ms) {
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path)) {
        error_ = "socket path too long";
        return State::NotRunning;
    }
    std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
    fd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd_ < 0) {
        error_ = std::strerror(errno);
        return State::NotRunning;
    }
    if (::connect(fd_, reinterpret_cast<const sockaddr*>(&addr), sizeof addr) != 0) {
        const int e = errno;
        error_ = std::strerror(e);
        ::close(fd_);
        fd_ = -1;
        return e == EACCES || e == EPERM ? State::NoAccess : State::NotRunning;
    }

    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
        while (!frames_.empty()) {
            const std::string body = std::move(frames_.front());
            frames_.pop_front();
            if (auto h = core::decode_hello(body)) {
                hello_ = std::move(*h);
                return State::Running;
            }
        }
        const Next n = fill(remaining_ms(deadline));
        if (n == Next::Timeout) {
            error_ = "no greeting from the daemon";
            return State::NoAnswer;
        }
        if (n != Next::Sample) {
            if (error_.empty()) error_ = "the connection closed before the daemon greeted it";
            return State::NoAnswer;
        }
    }
}

DaemonSocket::Next DaemonSocket::fill(int timeout_ms) {
    pollfd p{fd_, POLLIN, 0};
    const int r = ::poll(&p, 1, timeout_ms);
    if (r < 0) return errno == EINTR ? Next::Interrupted : Next::Closed;
    if (r == 0) return Next::Timeout;
    char chunk[65536];
    const ssize_t n = ::read(fd_, chunk, sizeof chunk);
    if (n < 0) {
        if (errno == EINTR) return Next::Interrupted;
        error_ = std::strerror(errno);
        return Next::Closed;
    }
    if (n == 0) return Next::Closed;
    buf_.append(chunk, std::size_t(n));
    std::vector<std::string> out;
    if (!core::extract_frames(buf_, out)) {
        error_ = "the daemon sent a frame that could not be read";
        return Next::Corrupt;
    }
    for (auto& f : out) frames_.push_back(std::move(f));
    return Next::Sample;   // "something arrived"
}

DaemonSocket::Next DaemonSocket::next_sample(int timeout_ms, core::Sample& out) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms < 0 ? 0 : timeout_ms);
    for (;;) {
        while (!frames_.empty()) {
            const std::string body = std::move(frames_.front());
            frames_.pop_front();
            if (auto s = core::decode_sample(body)) {
                out = std::move(*s);
                return Next::Sample;
            }
        }
        const Next n = fill(timeout_ms < 0 ? -1 : remaining_ms(deadline));
        if (n != Next::Sample) return n;
    }
}

}  // namespace bandsight::cli
