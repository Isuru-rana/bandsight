// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "collector.h"
#include "control_server.h"
#include "iface_class.h"
#include "netlink_stats.h"
#include "resolver.h"
#include "store.h"

namespace bandsight {

struct Prev {
    std::uint64_t rx = 0;
    std::uint64_t tx = 0;
};

struct IfaceObservation {
    int ifindex = 0;
    std::string name;
    IfaceKind kind = IfaceKind::Virtual;
    bool has_delta = false;   // false on the first observation of this interface
    std::uint64_t d_rx = 0;
    std::uint64_t d_tx = 0;
};

// Turns cumulative kernel counters into per-interval deltas. Pure apart from
// updating `prev`. An interface with no entry in `prev` is baselined and returned
// with has_delta = false; a counter that decreased is treated as a reset (delta 0).
std::vector<IfaceObservation> observe_ifaces(const std::vector<IfaceStats>& stats,
                                            std::map<int, Prev>& prev,
                                            const std::string& sysfs_root);

class DaemonLoop {
public:
    DaemonLoop(Store& store, std::string sysfs_root);

    // Loads and attaches the BPF collector. Safe to skip: tick() degrades to
    // interface-only accounting when this was never called or returned false.
    bool start_collector();

    // One 1 Hz iteration. Public so tests can drive it without a real clock.
    void tick(std::int64_t now);

    void request_stop();
    bool stopping() const;

    // Attaches the control-plane server. Not owned: the caller keeps it alive
    // for at least as long as this DaemonLoop.
    void attach_control(ControlServer* c);

private:
    // Logs a tick failure caught by tick()'s top-level catch, rate-limited so a
    // persistent failure (e.g. disk full) cannot fill the journal: first
    // occurrence logs immediately, then at most once a minute with a count of
    // what was suppressed in between.
    void log_tick_error(const std::string& what);

    // Deltas drained from the kernel but not yet durably written. drain_app()
    // deletes from the BPF map unconditionally, so if the SQLite write then
    // throws those bytes exist nowhere: the map no longer has them and nothing
    // else kept them (I6). The spec's argument that LOOKUP_AND_DELETE_BATCH
    // loses no increment covers the kernel-to-userspace hop and stops one
    // function later.
    //
    // Carried into the next tick and merged. Capped, because a database that is
    // permanently unwritable - a full disk, C4 - must degrade by dropping data
    // with a count in the log, not by growing until the daemon is OOM-killed.
    void retain_unwritten(const std::vector<IfaceDelta>& ifaces,
                          const std::vector<AppDelta>& apps);

    std::vector<IfaceDelta> pending_ifaces_;
    std::vector<AppDelta> pending_apps_;
    std::int64_t pending_dropped_ = 0;

    // Written rows that have not changed since we last wrote them. upsert_iface
    // ran for every interface and upsert_app for every active app on EVERY tick,
    // each its own implicit transaction, and ON CONFLICT DO UPDATE rewrote the
    // row even when nothing differed - on a docker host with 100 veths that is
    // 100+ WAL commits a second, forever, for rows that never change (I10).
    //
    // Keyed by ifindex rather than name because a name is reused with a fresh
    // ifindex (a wg-quick or docker restart), and by exe because that is what
    // identifies an application row.
    std::unordered_map<int, std::pair<std::string, std::string>> iface_written_;
    std::unordered_map<std::string, std::int64_t> app_ids_;

    // Test-only: the memo is invisible from outside otherwise, and "did this
    // tick write anything" is the entire behaviour under test.
    std::int64_t iface_writes_ = 0;
    std::int64_t app_writes_ = 0;

public:
    std::int64_t ifaceWritesForTest() const { return iface_writes_; }
    std::int64_t appWritesForTest() const { return app_writes_; }
    std::size_t pendingIfaceDeltasForTest() const { return pending_ifaces_.size(); }
    std::size_t pendingAppDeltasForTest() const { return pending_apps_.size(); }
    std::int64_t pendingDroppedForTest() const { return pending_dropped_; }
    // Test-only: reaching the retry path through tick() needs real traffic
    // between two real netlink reads, which is not something a test can arrange.
    void retainForTest(const std::vector<IfaceDelta>& ifaces,
                       const std::vector<AppDelta>& apps) {
        retain_unwritten(ifaces, apps);
    }

private:
    Store& store_;
    std::string sysfs_root_;
    std::map<int, Prev> prev_;      // ifindex -> last cumulative reading; presence = baselined

    // Rollup/vacuum cadence is gated on CLOCK_MONOTONIC, not the tick's wall-clock
    // `now`, so an NTP step backwards cannot stall it (see I5). The wall clock is
    // still what gets passed to rollup_tier/enforce_retention for bucket stamps.
    std::chrono::steady_clock::time_point last_rollup_mono_{};
    bool rollup_seeded_ = false;
    std::int64_t last_vacuum_day_ = 0;  // bucket_start(now, kRes1d) of the last vacuum

    bool tick_error_logged_ = false;
    std::chrono::steady_clock::time_point last_tick_error_log_{};
    std::uint64_t suppressed_tick_errors_ = 0;

    std::atomic<bool> stop_{false};
    Collector collector_;
    Resolver resolver_;
    ControlServer* control_ = nullptr;
};

// Opens the store at db_path, migrates, and runs until SIGTERM/SIGINT.
// socket_path is where the control server listens. If the socket cannot be
// created (e.g. no permission on its parent directory), accounting continues
// unaffected; only live GUI frames are unavailable.
int run_daemon(const std::string& db_path, const std::string& socket_path);

}  // namespace bandsight
