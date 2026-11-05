// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <vector>

namespace bandsight::core {

// What an application row is called: its friendly name, else the executable's
// file name, else "(unknown)". Never empty, so an application the resolver
// could not name still occupies a readable row.
std::string app_display_name(const std::string& exe, const std::string& name);

struct LabelledExe {
    std::string label;   // usually app_display_name()
    std::string exe;
};

// Labels for a list of applications, disambiguated ONLY where they collide: two
// rows both called "node" gain the shortest tail of their directories that
// tells them apart ("node (v20/bin)", "node (v24/bin)"), up to three
// components, else the whole directory. Every other label is returned as given.
std::vector<std::string> disambiguated_labels(const std::vector<LabelledExe>& apps);

}  // namespace bandsight::core
