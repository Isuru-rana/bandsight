// SPDX-License-Identifier: GPL-3.0-or-later
// bandsight-cli: the daemon's history and live rates in a terminal, for
// headless machines. Links bandsight_common only - no Qt.
#include <unistd.h>

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "commands.h"

namespace {
void on_signal(int) { bandsight::cli::request_stop(); }
}  // namespace

int main(int argc, char** argv) {
    // Ctrl-C ends `live` cleanly. No SA_RESTART: the blocking wait must return.
    struct sigaction sa {};
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    bandsight::cli::Env env;
    env.tty = ::isatty(STDOUT_FILENO) == 1;
    const char* term = std::getenv("TERM");
    env.no_color = std::getenv("NO_COLOR") != nullptr || (term && std::strcmp(term, "dumb") == 0);
    env.now = std::int64_t(std::time(nullptr));
    // For the test suite only: measure periods from a fixed clock, so output
    // against a fixture database is reproducible. Not documented on purpose.
    if (const char* fixed = std::getenv("BANDSIGHT_CLI_NOW")) {
        env.now = std::atoll(fixed);
        env.clock_fixed = true;
    }

    const std::vector<std::string> args(argv, argv + argc);
    return bandsight::cli::run(args, env);
}
