// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace bandsight {

struct AppKey {
    std::uint64_t cgroup_id = 0;
    std::uint32_t tgid = 0;
    std::uint32_t ifindex = 0;
};

struct AppVal {
    std::uint64_t rx = 0;
    std::uint64_t tx = 0;
    std::uint64_t rx_pkts = 0;
    std::uint64_t tx_pkts = 0;
};

// One map_sock entry. Layout must match struct sock_key / sock_val in
// bandsight.bpf.c byte for byte: the batch drain copies raw kernel memory in.
struct SockKey {
    std::uint64_t cookie = 0;
};

struct SockVal {
    std::uint64_t cgroup_id = 0;
    std::uint32_t tgid = 0;
    std::uint32_t ifindex = 0;
    std::uint8_t family = 0;
    std::uint8_t proto = 0;
    std::uint16_t rport = 0;   // host byte order
    std::uint8_t raddr[16] = {};   // IPv4 stored v4-mapped
    std::uint32_t pad = 0;
    std::uint64_t rx = 0;
    std::uint64_t tx = 0;
};
static_assert(sizeof(SockVal) == 56, "must match struct sock_val in bandsight.bpf.c");

// One process identity event surfaced from the BPF ring buffer: either an
// exec (new tgid, known path) or an exit (tgid gone, exe unused).
struct ProcEvent {
    enum class Type { Exec, Exit };
    Type type = Type::Exec;
    std::uint32_t tgid = 0;
    std::string exe;
};

// The daemon's own network namespace, as the kernel identifies it: the inode
// of /proc/self/ns/net (shown by readlink as net:[N]). 0 if it cannot be read.
std::uint32_t own_netns_inode();

class Collector {
public:
    Collector();
    ~Collector();
    Collector(const Collector&) = delete;
    Collector& operator=(const Collector&) = delete;

    // Loads and attaches. Returns false and logs the failing hook rather than
    // throwing: the daemon must keep serving interface totals when BPF is
    // unavailable.
    bool load();
    bool degraded() const { return degraded_; }

    // Atomically removes and returns every entry accumulated since the last call.
    std::vector<std::pair<AppKey, AppVal>> drain_app();

    // Same contract as drain_app(), for map_sock: every per-socket entry since
    // the last call, removed atomically. Also reads sock_dropped and logs growth
    // (rate-limited) - bytes the host breakdown is missing.
    std::vector<std::pair<SockKey, SockVal>> drain_sock();

    // Non-blocking consume of the exec/exit ring buffer. Returns an empty
    // vector without allocating when degraded or the ring buffer never came up.
    std::vector<ProcEvent> drain_events();

private:
    // Reads the BPF-side dropped-event counter (events_dropped) and, if it
    // has grown since the last read, logs the delta - rate-limited the same
    // way DaemonLoop::log_tick_error is, so a sustained exec storm (e.g. a
    // parallel build) cannot flood the journal.
    void check_dropped_events();

    // sock_dropped: bytes lost to a full map_sock. Same rate limit as above.
    void check_sock_dropped();

    struct Impl;
    Impl* impl_ = nullptr;
    bool degraded_ = true;
};

}  // namespace bandsight
