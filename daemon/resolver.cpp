// SPDX-License-Identifier: GPL-3.0-or-later
#include "resolver.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace bandsight {
namespace {
namespace fs = std::filesystem;

// Cache size above which sweep_exited() prunes dead tgids even without a
// matching exit event (see FOLLOWUP I3): a dropped BPF exit event otherwise
// leaks its exe_cache_ entry forever.
constexpr std::size_t kExeCacheMax = 8192;

// Shared by seed_from_proc() and exe_for()'s lazy fallback: reads the exe
// symlink for a tgid, returning "" (never throwing) when it cannot be read.
std::string read_exe_link(std::uint32_t tgid) {
    char buf[4096];
    const std::string link = "/proc/" + std::to_string(tgid) + "/exe";
    const ssize_t n = ::readlink(link.c_str(), buf, sizeof(buf) - 1);
    if (n <= 0) return {};
    return std::string(buf, static_cast<std::size_t>(n));
}

// True if /proc/<tgid> still exists, i.e. the process has not exited.
bool proc_dir_exists(std::uint32_t tgid) {
    struct stat st{};
    return ::stat(("/proc/" + std::to_string(tgid) + "/").c_str(), &st) == 0;
}

// cgroup v2's per-process file has exactly one line, "0::<path>", where
// <path> is relative to the cgroupfs mount ("/user.slice/..." or "/" for the
// root cgroup). One open, no walk, no inode comparison - but only works while
// the tgid is still alive. Returns "" (never throws) on any failure, so the
// caller can fall back to the inode walk.
//
// The result is normalised to the same absolute form find_cgroup_path()
// below produces (e.g. "/sys/fs/cgroup/user.slice/..."), so apps.cgroup_hint
// gets one consistent format regardless of which source resolved it.
std::string read_cgroup_from_proc(std::uint32_t tgid) {
    std::ifstream f("/proc/" + std::to_string(tgid) + "/cgroup");
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("0::", 0) != 0) continue;  // not the v2 unified line
        const std::string rel = line.substr(3);
        if (rel.empty()) return "";
        if (rel == "/") return "/sys/fs/cgroup";
        return "/sys/fs/cgroup" + rel;
    }
    return "";
}

// Cgroup ids are inode numbers of the cgroupfs directory. We do not know the
// path in advance, so on a cache miss we walk the tree once looking for the
// matching inode. Any filesystem error degrades to "not found" rather than
// throwing, per the class's fallback-only contract.
std::string find_cgroup_path(std::uint64_t cgroup_id) {
    struct stat root_st{};
    if (::stat("/sys/fs/cgroup", &root_st) == 0 &&
        static_cast<std::uint64_t>(root_st.st_ino) == cgroup_id) {
        return "/sys/fs/cgroup";
    }

    std::error_code ec;
    fs::recursive_directory_iterator it(
        "/sys/fs/cgroup", fs::directory_options::skip_permission_denied, ec);
    if (ec) return "";
    const fs::recursive_directory_iterator end;

    for (; !ec && it != end; it.increment(ec)) {
        std::error_code type_ec;
        if (!it->is_directory(type_ec) || type_ec) continue;

        struct stat st{};
        if (::stat(it->path().c_str(), &st) != 0) continue;
        if (static_cast<std::uint64_t>(st.st_ino) == cgroup_id) {
            return it->path().string();
        }
    }
    return "";
}

}  // namespace

void Resolver::seed_from_proc() {
    std::error_code ec;
    fs::directory_iterator it("/proc", ec);
    if (ec) return;
    const fs::directory_iterator end;

    for (; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.empty() || !std::isdigit(static_cast<unsigned char>(name[0]))) continue;

        const auto tgid = static_cast<std::uint32_t>(std::strtoul(name.c_str(), nullptr, 10));
        std::string exe = read_exe_link(tgid);
        if (!exe.empty()) exe_cache_[tgid] = std::move(exe);
    }
}

void Resolver::note_exec(std::uint32_t tgid, std::string exe) {
    // The spec's stable identity key is readlink("/proc/<pid>/exe"), not the
    // raw execve() path the tracepoint hands us: a symlink (python3 ->
    // python3.13), an alternatives-managed path, or a relative "./tool" would
    // otherwise split one binary's totals across two `apps` rows. Prefer the
    // resolved link while the process is still alive; fall back to the
    // tracepoint path only when it has already exited (or never existed -
    // /proc/<tgid>/exe is gone either way).
    std::string resolved = read_exe_link(tgid);
    if (!resolved.empty()) exe = std::move(resolved);
    if (exe.empty()) return;
    exited_.erase(tgid);
    exe_cache_[tgid] = std::move(exe);
}

void Resolver::note_exit(std::uint32_t tgid) { exited_[tgid] = true; }

std::string Resolver::exe_for(std::uint32_t tgid) {
    const auto cached = exe_cache_.find(tgid);
    if (cached != exe_cache_.end()) return cached->second;

    std::string result = read_exe_link(tgid);
    if (result.empty()) result = "unknown";

    exe_cache_.emplace(tgid, result);
    return result;
}

std::string Resolver::cgroup_path(std::uint32_t tgid, std::uint64_t cgroup_id) {
    const auto cached = cgroup_cache_.find(cgroup_id);
    if (cached != cgroup_cache_.end()) return cached->second;

    // Prefer the tgid we already have: one open, no walk. Only fall back to
    // the expensive inode walk when the tgid has already exited (the common
    // case for the walk finding nothing at all - see FOLLOWUP I4).
    std::string result = read_cgroup_from_proc(tgid);
    if (result.empty()) {
        try {
            result = find_cgroup_path(cgroup_id);
        } catch (...) {
            result.clear();
        }
    }

    cgroup_cache_.emplace(cgroup_id, result);
    return result;
}

void Resolver::sweep_exited() {
    for (const auto& [tgid, dead] : exited_) {
        if (dead) exe_cache_.erase(tgid);
    }
    exited_.clear();

    // Belt-and-braces bound for FOLLOWUP I3: a dropped BPF exit event means a
    // dead tgid can reach here without ever appearing in exited_. Only pay
    // for a full stat()-per-entry pass once the cache is actually oversized,
    // so this stays free on every ordinary tick.
    if (exe_cache_.size() > kExeCacheMax) {
        for (auto it = exe_cache_.begin(); it != exe_cache_.end();) {
            if (!proc_dir_exists(it->first)) {
                it = exe_cache_.erase(it);
            } else {
                ++it;
            }
        }
        // If still over the bound, every remaining entry belongs to a live
        // process - nothing left to reclaim without an LRU, which is more
        // machinery than this (implausible) case warrants.
    }
}

}  // namespace bandsight
