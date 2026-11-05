// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/app_labels.h"

#include <map>
#include <set>

namespace bandsight::core {

namespace {

// Past this the label is longer than anything a column can show, and the tooltip
// is the honest place for the rest.
constexpr int kMaxComponents = 3;

std::vector<std::string> dir_components(const std::string& exe) {
    std::vector<std::string> parts;
    std::size_t at = 0;
    while (at <= exe.size()) {
        const std::size_t slash = exe.find('/', at);
        const std::size_t end = slash == std::string::npos ? exe.size() : slash;
        if (end > at) parts.push_back(exe.substr(at, end - at));
        if (slash == std::string::npos) break;
        at = slash + 1;
    }
    if (!parts.empty()) parts.pop_back();   // drop the binary itself
    return parts;
}

std::string join(const std::vector<std::string>& parts, std::size_t from) {
    std::string out;
    for (std::size_t i = from; i < parts.size(); ++i) {
        if (i > from) out += '/';
        out += parts[i];
    }
    return out;
}

std::string tail(const std::vector<std::string>& parts, int n) {
    const std::size_t take = std::min<std::size_t>(std::size_t(n), parts.size());
    return join(parts, parts.size() - take);
}

}  // namespace

std::string app_display_name(const std::string& exe, const std::string& name) {
    if (!name.empty()) return name;
    if (!exe.empty()) return exe.substr(exe.rfind('/') + 1);   // npos + 1 == 0
    return "(unknown)";
}

std::vector<std::string> disambiguated_labels(const std::vector<LabelledExe>& apps) {
    std::vector<std::string> out;
    out.reserve(apps.size());
    for (const LabelledExe& a : apps) out.push_back(a.label);

    // Which rows share a name. Only those are touched; every other label is
    // exactly what it was.
    std::map<std::string, std::vector<std::size_t>> by_name;
    for (std::size_t i = 0; i < out.size(); ++i) by_name[out[i]].push_back(i);

    for (const auto& [label, rows] : by_name) {
        if (rows.size() < 2) continue;

        std::vector<std::vector<std::string>> dirs;
        for (std::size_t r : rows) dirs.push_back(dir_components(apps[r].exe));

        // The shortest tail that separates them all. Two nvm installs differ
        // only near the root, so this can run out - hence the cap and the
        // fallback below.
        int depth = 1;
        for (; depth <= kMaxComponents; ++depth) {
            std::set<std::string> seen;
            bool unique = true;
            for (const auto& d : dirs) {
                if (!seen.insert(tail(d, depth)).second) { unique = false; break; }
            }
            if (unique) break;
        }

        // The cap can leave rows still identical - two nvm installs share
        // "node/v24.18.0/bin" and differ only near the root. Those fall back to
        // the whole directory, which ALWAYS separates them: rows collide only
        // when they share a name, and a shared name in a shared directory would
        // mean the same executable, which cannot happen since exe is the
        // identity of the row.
        const bool use_full_dir = depth > kMaxComponents;

        for (std::size_t i = 0; i < rows.size(); ++i) {
            const std::string suffix = use_full_dir ? join(dirs[i], 0) : tail(dirs[i], depth);
            // A row whose executable is unknown has no directory to show; it
            // keeps the bare name rather than gaining an empty bracket.
            if (suffix.empty()) continue;
            out[rows[i]] = out[rows[i]] + " (" + suffix + ")";
        }
    }
    return out;
}

}  // namespace bandsight::core
