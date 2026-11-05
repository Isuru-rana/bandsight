// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace bandsight::cli {

// Exit codes, as documented in --help.
constexpr int kExitOk = 0;
constexpr int kExitRuntime = 1;
constexpr int kExitUsage = 2;

// What the process was started with, gathered by main() so the commands can be
// driven from a test with a fixed clock.
struct Env {
    bool tty = false;        // stdout is a terminal
    bool no_color = false;   // NO_COLOR set, or TERM=dumb
    std::int64_t now = 0;    // the clock history periods are measured from
};

// Set by the SIGINT/SIGTERM handler main() installs; `live` stops on it.
void request_stop();

// Runs one invocation. stdout and stderr text are written as they are produced
// (live needs that); the return value is the exit code.
int run(const std::vector<std::string>& argv, const Env& env);

}  // namespace bandsight::cli
