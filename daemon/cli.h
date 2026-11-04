// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>

namespace bandsight {

// bandsightd's command line. Parsed completely before anything else runs, so
// --version, --help and a typo all exit without opening the database, binding
// the socket or loading BPF.
struct CliOptions {
    enum class Action { Run, Version, Help, Usage };
    Action action = Action::Run;
    std::string db_path = "/var/lib/bandsight/bandsight.db";
    std::string socket_path = "/run/bandsightd/sock";
    std::string error;   // Usage only: what was wrong
};

CliOptions parse_cli(int argc, const char* const* argv);
std::string usage_text();
std::string version_text();   // "bandsightd 0.4.0"

}  // namespace bandsight
