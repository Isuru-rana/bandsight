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

// One process identity event surfaced from the BPF ring buffer: either an
// exec (new tgid, known path) or an exit (tgid gone, exe unused).
struct ProcEvent {
    enum class Type { Exec, Exit };
    Type type = Type::Exec;
    std::uint32_t tgid = 0;
    std::string exe;
};

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

    // Non-blocking consume of the exec/exit ring buffer. Returns an empty
    // vector without allocating when degraded or the ring buffer never came up.
    std::vector<ProcEvent> drain_events();

private:
    // Reads the BPF-side dropped-event counter (events_dropped) and, if it
    // has grown since the last read, logs the delta - rate-limited the same
    // way DaemonLoop::log_tick_error is, so a sustained exec storm (e.g. a
    // parallel build) cannot flood the journal.
    void check_dropped_events();

    struct Impl;
    Impl* impl_ = nullptr;
    bool degraded_ = true;
};

}  // namespace bandsight
