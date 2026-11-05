// SPDX-License-Identifier: GPL-3.0-or-later
#include "terminal.h"

#include <sys/ioctl.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstring>

namespace bandsight::cli {

namespace {
// The terminal restore_active() reaches. One full-screen view at a time.
std::atomic<Terminal*> g_active{nullptr};

void write_all(int fd, const char* p, std::size_t n) {
    while (n > 0) {
        const ssize_t w = ::write(fd, p, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            return;
        }
        p += w;
        n -= std::size_t(w);
    }
}
}  // namespace

termios raw_mode(const termios& from) {
    termios t = from;
    t.c_iflag &= ~tcflag_t(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    t.c_lflag &= ~tcflag_t(ECHO | ICANON | IEXTEN | ISIG);
    t.c_cflag |= CS8;
    // Output processing stays on: the view positions every line itself, and a
    // stray message from elsewhere still lands at the start of a line.
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    return t;
}

bool Terminal::enter(std::string* error) {
    if (active_) return true;
    if (::tcgetattr(in_, &saved_) != 0) {
        *error = std::strerror(errno);
        return false;
    }
    const termios raw = raw_mode(saved_);
    if (::tcsetattr(in_, TCSAFLUSH, &raw) != 0) {
        *error = std::strerror(errno);
        return false;
    }
    active_ = true;
    g_active.store(this);
    write(kEnter);
    return true;
}

void Terminal::restore() {
    if (!active_) return;
    active_ = false;
    write_all(out_, kLeave, std::strlen(kLeave));
    ::tcsetattr(in_, TCSADRAIN, &saved_);
    Terminal* self = this;
    g_active.compare_exchange_strong(self, nullptr);
}

void Terminal::restore_active() {
    if (Terminal* t = g_active.load()) t->restore();
}

bool Terminal::size(int& cols, int& rows) const {
    winsize ws{};
    if (::ioctl(out_, TIOCGWINSZ, &ws) != 0 || ws.ws_col == 0 || ws.ws_row == 0) return false;
    cols = ws.ws_col;
    rows = ws.ws_row;
    return true;
}

void Terminal::write(const std::string& s) const { write_all(out_, s.data(), s.size()); }

}  // namespace bandsight::cli
