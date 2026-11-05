// SPDX-License-Identifier: GPL-3.0-or-later
// The terminal guard `bandsight-cli top` runs under, on a pseudo-terminal:
// raw mode on entering, the original settings and the normal screen back on
// every way out - restore(), the destructor on return and on an exception,
// and restore_active() (the std::terminate path) - and exactly once.
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cstring>
#include <stdexcept>
#include <string>

#include "check.h"
#include "terminal.h"

using bandsight::cli::Terminal;

namespace {

struct Pty {
    int master = -1, slave = -1;
    Pty() {
        CHECK(::openpty(&master, &slave, nullptr, nullptr, nullptr) == 0);
        ::fcntl(master, F_SETFL, O_NONBLOCK);
    }
    ~Pty() {
        ::close(master);
        ::close(slave);
    }
    // Everything the program wrote to the terminal so far.
    std::string drain() const {
        std::string s;
        char buf[4096];
        for (;;) {
            pollfd p{master, POLLIN, 0};
            if (::poll(&p, 1, 50) <= 0) break;
            const ssize_t n = ::read(master, buf, sizeof buf);
            if (n <= 0) break;
            s.append(buf, std::size_t(n));
        }
        return s;
    }
    termios mode() const {
        termios t{};
        ::tcgetattr(slave, &t);
        return t;
    }
};

bool same_mode(const termios& a, const termios& b) {
    return a.c_iflag == b.c_iflag && a.c_oflag == b.c_oflag && a.c_cflag == b.c_cflag &&
           a.c_lflag == b.c_lflag && a.c_cc[VMIN] == b.c_cc[VMIN] && a.c_cc[VTIME] == b.c_cc[VTIME];
}

bool is_raw(const termios& t) {
    return !(t.c_lflag & ICANON) && !(t.c_lflag & ECHO) && !(t.c_lflag & ISIG) &&
           !(t.c_iflag & IXON) && !(t.c_iflag & ICRNL);
}

std::size_t count(const std::string& s, const std::string& what) {
    std::size_t n = 0;
    for (std::size_t at = 0; (at = s.find(what, at)) != std::string::npos; at += what.size()) ++n;
    return n;
}

void enter_and_restore() {
    Pty p;
    const termios before = p.mode();
    CHECK(!is_raw(before));
    {
        Terminal t(p.slave, p.slave);
        std::string why;
        CHECK(t.enter(&why));
        CHECK(t.active());
        CHECK(is_raw(p.mode()));
        CHECK(p.drain() == Terminal::kEnter);
        t.restore();
        CHECK(!t.active());
        CHECK(same_mode(p.mode(), before));
        CHECK(p.drain() == Terminal::kLeave);
        // Twice is once: nothing more is written, nothing changes.
        t.restore();
        CHECK(p.drain().empty());
    }
    // Nor does the destructor after an explicit restore.
    CHECK(p.drain().empty());
    CHECK(same_mode(p.mode(), before));
}

void destructor_restores() {
    Pty p;
    const termios before = p.mode();
    {
        Terminal t(p.slave, p.slave);
        std::string why;
        CHECK(t.enter(&why));
        p.drain();
    }
    CHECK(same_mode(p.mode(), before));
    CHECK(p.drain() == Terminal::kLeave);
}

void exception_restores() {
    Pty p;
    const termios before = p.mode();
    bool caught = false;
    try {
        Terminal t(p.slave, p.slave);
        std::string why;
        CHECK(t.enter(&why));
        p.drain();
        throw std::runtime_error("boom");
    } catch (const std::runtime_error&) {
        caught = true;
        // Restored before the handler runs, so a message printed here lands on
        // the normal screen.
        CHECK(same_mode(p.mode(), before));
    }
    CHECK(caught);
    const std::string out = p.drain();
    CHECK(count(out, Terminal::kLeave) == 1);
}

void restore_active_restores() {
    Pty p;
    const termios before = p.mode();
    {
        Terminal t(p.slave, p.slave);
        std::string why;
        CHECK(t.enter(&why));
        p.drain();
        // What the std::terminate handler calls.
        Terminal::restore_active();
        CHECK(!t.active());
        CHECK(same_mode(p.mode(), before));
        CHECK(p.drain() == Terminal::kLeave);
    }
    CHECK(p.drain().empty());
    // With nothing entered it does nothing.
    Terminal::restore_active();
    CHECK(p.drain().empty());
}

void not_a_terminal() {
    int fds[2];
    CHECK(::pipe(fds) == 0);
    {
        Terminal t(fds[0], fds[1]);
        std::string why;
        CHECK(!t.enter(&why));
        CHECK(!why.empty());
        CHECK(!t.active());
    }
    ::fcntl(fds[0], F_SETFL, O_NONBLOCK);
    char c;
    CHECK(::read(fds[0], &c, 1) < 0);   // nothing written
    ::close(fds[0]);
    ::close(fds[1]);
}

void size_of_pty() {
    Pty p;
    winsize ws{};
    ws.ws_col = 100;
    ws.ws_row = 30;
    ::ioctl(p.slave, TIOCSWINSZ, &ws);
    Terminal t(p.slave, p.slave);
    int w = 0, h = 0;
    CHECK(t.size(w, h) && w == 100 && h == 30);
}

}  // namespace

int main() {
    enter_and_restore();
    destructor_restores();
    exception_restores();
    restore_active_restores();
    not_a_terminal();
    size_of_pty();
    if (g_failures) std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    return g_failures ? 1 : 0;
}
