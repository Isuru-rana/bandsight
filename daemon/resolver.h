// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>

namespace bandsight {

// Maps kernel identifiers surfaced by the BPF collector (tgid, cgroup id) to
// human-meaningful strings: an executable path and a cgroup path.
//
// Three identity sources, in priority order:
//   1. seed_from_proc() - a one-time /proc walk at startup for processes
//      already running before the daemon attached.
//   2. note_exec() - sched_process_exec tracepoint events, covering every
//      process launched afterwards, including ones that exit before the
//      next drain.
//   3. exe_for()'s lazy /proc/<tgid>/exe fallback - anything the first two
//      missed.
class Resolver {
public:
    // Source 1: one-time walk of /proc for processes already running at startup.
    void seed_from_proc();

    // Source 2: exec events, which cover short-lived processes. Prefers the
    // resolved /proc/<tgid>/exe link over the raw execve() path passed in;
    // the latter is used only when the process has already exited.
    void note_exec(std::uint32_t tgid, std::string exe);

    // Marks a tgid dead. The identity is retained so the final byte drain can
    // still be attributed; it is dropped on the following sweep.
    void note_exit(std::uint32_t tgid);

    // Source 3: lazy /proc/<tgid>/exe fallback. Returns "unknown" if the process
    // is gone and nothing was cached. Requires CAP_SYS_PTRACE for processes owned
    // by other users.
    std::string exe_for(std::uint32_t tgid);

    // Empty string when the id cannot be mapped. Never throws. Prefers reading
    // /proc/<tgid>/cgroup (one open, no walk) and falls back to walking
    // /sys/fs/cgroup by inode only when that read fails, i.e. the tgid has
    // already exited. Both sources are normalised to the same absolute-path
    // format before being cached, so callers can't tell which one resolved it.
    std::string cgroup_path(std::uint32_t tgid, std::uint64_t cgroup_id);

    // Drops identities for tgids that exited before the previous drain.
    void sweep_exited();

private:
    std::unordered_map<std::uint32_t, std::string> exe_cache_;
    std::unordered_map<std::uint32_t, bool> exited_;
    std::unordered_map<std::uint64_t, std::string> cgroup_cache_;
};

}  // namespace bandsight
