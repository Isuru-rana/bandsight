// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>

#include "cli.h"
#include "daemon_loop.h"

int main(int argc, char** argv) {
    using bandsight::CliOptions;
    const CliOptions o = bandsight::parse_cli(argc, argv);
    switch (o.action) {
    case CliOptions::Action::Version:
        std::printf("%s\n", bandsight::version_text().c_str());
        return 0;
    case CliOptions::Action::Help:
        std::fputs(bandsight::usage_text().c_str(), stdout);
        return 0;
    case CliOptions::Action::Usage:
        std::fprintf(stderr, "bandsightd: %s\n%s", o.error.c_str(),
                     bandsight::usage_text().c_str());
        return 2;
    case CliOptions::Action::Run:
        break;
    }
    return bandsight::run_daemon(o.db_path, o.socket_path);
}
