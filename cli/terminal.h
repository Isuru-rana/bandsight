// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <termios.h>

namespace bandsight::cli {

// A terminal taken over for a full-screen view: raw input (no echo, no line
// buffering, no signals from Ctrl-C - it arrives as a key), the alternate
// screen, the cursor hidden. restore() puts every one of those back.
//
// restore() is idempotent and safe to call from a signal handler or a
// std::terminate handler: it is a write() and a tcsetattr() and nothing else.
// The destructor calls it, so leaving the scope by return or by an exception
// restores the terminal too; restore_active() reaches the one terminal that is
// currently entered, for the paths that have no scope to unwind.
class Terminal {
public:
    // Escape sequences written on entering and leaving. Public for the tests.
    static constexpr const char* kEnter = "\033[?1049h\033[?25l\033[2J";
    static constexpr const char* kLeave = "\033[0m\033[?25h\033[?1049l";

    Terminal(int in_fd, int out_fd) : in_(in_fd), out_(out_fd) {}
    ~Terminal() { restore(); }
    Terminal(const Terminal&) = delete;
    Terminal& operator=(const Terminal&) = delete;

    // false with *error set when in_fd is not a terminal or cannot be set up;
    // nothing is changed then.
    bool enter(std::string* error);
    void restore();
    bool active() const { return active_; }

    // Columns and rows; false when the size cannot be read.
    bool size(int& cols, int& rows) const;

    // All of s, retrying on EINTR and short writes.
    void write(const std::string& s) const;

    static void restore_active();

private:
    int in_, out_;
    termios saved_{};
    volatile bool active_ = false;
};

// Raw mode as enter() sets it, from the settings it started with.
termios raw_mode(const termios& from);

}  // namespace bandsight::cli
