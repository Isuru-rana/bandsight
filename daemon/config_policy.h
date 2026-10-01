// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>

namespace bandsight {

// Gate for the one write path the GUI has into the daemon's database
// (spec §7's cap_bytes / cycle_start_day, reached through set_config on the
// control socket).
//
// The socket is group-readable, so anything in group `bandsight` can send this,
// and the daemon runs as root: the allowlist is the security boundary, not a
// convenience. schema_version in particular must never be settable - rewriting
// it would make a future migration read the database as a shape it is not.
//
// Returns an empty string when the write is allowed, or the reason to send back.
std::string validate_config(const std::string& key, const std::string& value);

}  // namespace bandsight
