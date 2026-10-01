// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>

namespace bandsight {

// The label an application carries in the Usage tab, derived from its
// executable path.
//
// The basename is right almost always, and wrong in one recurring way: software
// installed per-version puts the version where the name belongs. Claude Code's
// executable is literally
// /home/user/.local/share/claude/versions/2.1.247, so the basename is
// "2.1.247" - and because each upgrade is a new path, every version becomes its
// own apps row sharing no identity with the last. Twenty of them accumulated on
// the development machine before the Usage tab made it visible (FOLLOWUP U3).
//
// So: when the basename carries no name - a bare version, or a generic wrapper
// directory like bin/ or current/ - walk up for the first component that does.
// Everything else keeps the basename it always had.
std::string app_name_from_exe(const std::string& exe);

}  // namespace bandsight
