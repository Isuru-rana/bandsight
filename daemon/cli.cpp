// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli.h"

#include <cstring>

#include "build_info.h"

namespace bandsight {

CliOptions parse_cli(int argc, const char* const* argv) {
    CliOptions o;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (std::strcmp(a, "--version") == 0) {
            o.action = CliOptions::Action::Version;
            return o;
        }
        if (std::strcmp(a, "--help") == 0 || std::strcmp(a, "-h") == 0) {
            o.action = CliOptions::Action::Help;
            return o;
        }
        const bool db = std::strcmp(a, "--db") == 0;
        if (db || std::strcmp(a, "--socket") == 0) {
            // A flag with no value used to be skipped, starting the daemon on
            // the default database - the opposite of what the flag asked for.
            if (i + 1 >= argc || argv[i + 1][0] == '\0' ||
                std::strncmp(argv[i + 1], "--", 2) == 0) {
                o.action = CliOptions::Action::Usage;
                o.error = std::string(a) + " needs a path";
                return o;
            }
            (db ? o.db_path : o.socket_path) = argv[++i];
            continue;
        }
        o.action = CliOptions::Action::Usage;
        o.error = std::string("unknown option: ") + a;
        return o;
    }
    return o;
}

std::string version_text() { return std::string("bandsightd ") + BANDSIGHT_VERSION; }

std::string usage_text() {
    return "Usage: bandsightd [--db PATH] [--socket PATH]\n"
           "       bandsightd --version | --help\n"
           "\n"
           "Per-application network usage collector. Normally started by systemd\n"
           "(bandsightd.service) as root.\n"
           "\n"
           "  --db PATH       SQLite database (default /var/lib/bandsight/bandsight.db)\n"
           "  --socket PATH   control socket (default /run/bandsightd/sock)\n"
           "  --version       print the version and exit\n"
           "  -h, --help      print this help and exit\n";
}

}  // namespace bandsight
