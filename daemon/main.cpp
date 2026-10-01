// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstring>
#include <string>

#include "daemon_loop.h"

int main(int argc, char** argv) {
    std::string db_path = "/var/lib/bandsight/bandsight.db";
    std::string socket_path = "/run/bandsightd/sock";
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--db") == 0 && i + 1 < argc) {
            db_path = argv[++i];
        } else if (std::strcmp(argv[i], "--socket") == 0 && i + 1 < argc) {
            socket_path = argv[++i];
        }
    }
    return bandsight::run_daemon(db_path, socket_path);
}
