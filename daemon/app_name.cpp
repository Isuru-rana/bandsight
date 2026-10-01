// SPDX-License-Identifier: GPL-3.0-or-later
#include "app_name.h"

#include <algorithm>
#include <cctype>
#include <vector>

namespace bandsight {
namespace {

// "2.1.247", "v3", "1_2", "20240115". Digits and separators only - anything
// with a letter in it (python3.14, node20) is a name, not a version.
bool is_bare_version(const std::string& s) {
    if (s.empty()) return false;
    std::size_t i = (s[0] == 'v' || s[0] == 'V') ? 1 : 0;
    if (i >= s.size()) return false;          // "v" alone is not a version
    bool digit_seen = false;
    for (; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (std::isdigit(c)) { digit_seen = true; continue; }
        if (c == '.' || c == '_' || c == '-') continue;
        return false;
    }
    return digit_seen;
}

// Directories that exist to hold a program rather than to name one.
bool is_wrapper_dir(const std::string& s) {
    static const std::vector<std::string> kWrappers = {
        "bin", "sbin", "libexec", "lib", "lib64", "current", "latest",
        "versions", "version", "releases", "release", "dist", "build",
        "node_modules", ".bin", "target", "out",
    };
    return std::find(kWrappers.begin(), kWrappers.end(), s) != kWrappers.end();
}

// Walking up far enough always reaches a filesystem root, which names nothing.
// Landing on one means the path had no name to find and the basename - however
// poor - is still the most specific thing available.
bool is_root_like(const std::string& s) {
    static const std::vector<std::string> kRoots = {
        "usr", "opt", "home", "local", "share", "srv", "var", "mnt", "media",
        "tmp", "run", "root", "snap", "app", "apps", "data", ".local", ".cache",
    };
    return std::find(kRoots.begin(), kRoots.end(), s) != kRoots.end();
}

std::vector<std::string> split(const std::string& path) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t slash = path.find('/', start);
        const std::string part = path.substr(
            start, slash == std::string::npos ? std::string::npos : slash - start);
        if (!part.empty() && part != ".") out.push_back(part);
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    return out;
}

}  // namespace

std::string app_name_from_exe(const std::string& exe) {
    const std::vector<std::string> parts = split(exe);
    if (parts.empty()) return exe;

    const std::string basename = parts.back();
    // The overwhelmingly common case, and it must stay exact: no path walking,
    // no allocation beyond the split, same answer as the substr this replaced.
    if (!is_bare_version(basename) && !is_wrapper_dir(basename)) return basename;

    for (std::size_t i = parts.size(); i-- > 0;) {
        const std::string& part = parts[i];
        if (is_bare_version(part) || is_wrapper_dir(part)) continue;
        if (is_root_like(part)) break;   // walked out of the install, not into it
        return part;
    }
    return basename;   // nothing better exists; keep what we had
}

}  // namespace bandsight
