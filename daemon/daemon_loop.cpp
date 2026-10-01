// SPDX-License-Identifier: GPL-3.0-or-later
#include "daemon_loop.h"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <ctime>
#include <exception>
#include <vector>

#include "app_name.h"
#include "config_policy.h"
#include "iface_class.h"
#include "json_frame.h"
#include "netlink_stats.h"
#include "rollup.h"
#include "time_buckets.h"
#include "unix_socket_control.h"

namespace bandsight {
namespace {
volatile std::sig_atomic_t g_stop = 0;
void on_signal(int) { g_stop = 1; }
}  // namespace

DaemonLoop::DaemonLoop(Store& store, std::string sysfs_root)
    : store_(store), sysfs_root_(std::move(sysfs_root)) {}

bool DaemonLoop::start_collector() {
    const bool ok = collector_.load();
    // Seed identities for processes already running before any exec event can
    // fire for them - the first of the resolver's three identity sources.
    resolver_.seed_from_proc();
    return ok;
}

// Wiring the config write path here, rather than in the control server, keeps
// the allowlist (config_policy) and the storage (Store) in one place and leaves
// the socket ignorant of both. The handler runs on the daemon thread inside
// poll(), so the Store call needs no synchronisation.
void DaemonLoop::attach_control(ControlServer* c) {
    control_ = c;
    if (!control_) return;
    control_->set_config_handler(
        [this](const std::string& key, const std::string& value) -> std::string {
            const std::string err = validate_config(key, value);
            if (!err.empty()) return err;
            try {
                store_.set_config(key, value);
            } catch (const std::exception& e) {
                // Spec policy is degrade, never crash: a failed config write is
                // reported to the client and the daemon keeps collecting.
                return std::string("write failed: ") + e.what();
            }
            return {};
        });
}

void DaemonLoop::request_stop() { stop_ = true; }
bool DaemonLoop::stopping() const { return stop_ || g_stop; }

std::vector<IfaceObservation> observe_ifaces(const std::vector<IfaceStats>& stats,
                                            std::map<int, Prev>& prev,
                                            const std::string& sysfs_root) {
    std::vector<IfaceObservation> out;
    out.reserve(stats.size());

    for (const auto& s : stats) {
        IfaceObservation o;
        o.ifindex = s.ifindex;
        o.name = s.name;
        o.kind = classify_iface(s.name, sysfs_root);

        const auto it = prev.find(s.ifindex);
        if (it == prev.end()) {
            // First time we have ever seen this interface - whether at startup or
            // hours later when a dongle is plugged in. Baseline it and report no
            // delta, or its entire lifetime counter lands in one bucket.
            prev.emplace(s.ifindex, Prev{s.rx_bytes, s.tx_bytes});
            out.push_back(std::move(o));
            continue;
        }

        // A decrease means the interface was recreated and its counters reset.
        o.d_rx = (s.rx_bytes >= it->second.rx) ? s.rx_bytes - it->second.rx : 0;
        o.d_tx = (s.tx_bytes >= it->second.tx) ? s.tx_bytes - it->second.tx : 0;
        o.has_delta = true;
        it->second.rx = s.rx_bytes;
        it->second.tx = s.tx_bytes;
        out.push_back(std::move(o));
    }

    return out;
}

void DaemonLoop::log_tick_error(const std::string& what) {
    const auto t = std::chrono::steady_clock::now();
    if (!tick_error_logged_ || t - last_tick_error_log_ >= std::chrono::minutes(1)) {
        if (suppressed_tick_errors_ > 0) {
            std::fprintf(stderr,
                         "bandsightd: tick failed: %s (%llu further occurrence(s) "
                         "suppressed in the last minute)\n",
                         what.c_str(),
                         static_cast<unsigned long long>(suppressed_tick_errors_));
        } else {
            std::fprintf(stderr, "bandsightd: tick failed: %s\n", what.c_str());
        }
        tick_error_logged_ = true;
        last_tick_error_log_ = t;
        suppressed_tick_errors_ = 0;
    } else {
        ++suppressed_tick_errors_;
    }
}

// Timestamps are deliberately NOT preserved: the deltas are re-attributed to the
// tick that finally writes them. A delta is a count of bytes since the last
// observation, not an event at an instant, so folding it into a later second
// moves it slightly in time while keeping every byte. Keeping the original
// timestamps would need a second write path and would reintroduce rows for
// buckets the rollup may already have closed.
void DaemonLoop::retain_unwritten(const std::vector<IfaceDelta>& ifaces,
                                  const std::vector<AppDelta>& apps) {
    // Roughly a minute of a busy machine. Past this the database is not coming
    // back on its own, and holding more only moves the failure from "some data
    // lost" to "the collector was OOM-killed".
    constexpr std::size_t kMaxPending = 8192;

    for (const auto& d : ifaces) {
        if (pending_ifaces_.size() >= kMaxPending) { ++pending_dropped_; continue; }
        pending_ifaces_.push_back(d);
    }
    for (const auto& d : apps) {
        if (pending_apps_.size() >= kMaxPending) { ++pending_dropped_; continue; }
        pending_apps_.push_back(d);
    }
    if (pending_dropped_ > 0) {
        std::fprintf(stderr,
                     "bandsightd: %lld drained deltas discarded; the database has "
                     "been unwritable long enough to fill the retry buffer\n",
                     static_cast<long long>(pending_dropped_));
    }
}

void DaemonLoop::tick(std::int64_t now) {
    std::vector<IfaceStats> stats;
    try {
        stats = read_iface_stats();
    } catch (const std::exception& e) {
        // Log and continue with no interface stats, rather than returning (I7).
        // The two accounting dimensions are meant to be independent, and the
        // early return skipped the event drain, the map drain, the write, the
        // rollup and the broadcast along with them. A persistent netlink failure
        // then let map_app fill to its 16384 entries, after which updates fail
        // and bytes vanish with nothing said.
        std::fprintf(stderr, "bandsightd: netlink read failed: %s; continuing "
                             "with per-application accounting only\n", e.what());
        stats.clear();
    }

    // Everything below touches SQLite, which degrades (SQLITE_FULL, SQLITE_BUSY,
    // a constraint violation) rather than being an "impossible" condition worth
    // crashing over. One tick of data is lost instead of the process - see C4.
    // Store::write_samples/rollup_tier/enforce_retention each wrap their own
    // BEGIN in a try/ROLLBACK, so a throw from any of them has already left no
    // transaction open by the time it reaches here; the next tick starts clean.
    try {
        // Seed the rollup clock on the first tick so the first rollup happens a
        // minute from now, not on the very next tick. Monotonic, not wall-clock:
        // an NTP step backwards must not stall the rollup/retention cadence.
        const auto now_mono = std::chrono::steady_clock::now();
        if (!rollup_seeded_) {
            last_rollup_mono_ = now_mono;
            rollup_seeded_ = true;
        }

        std::vector<IfaceDelta> deltas;
        for (const auto& o : observe_ifaces(stats, prev_, sysfs_root_)) {
            // Registered unconditionally: excluded from accounting is not excluded
            // from existing, and the UI needs a row to label.
            // Only on first sight or on a change: see iface_written_ for why.
            {
                const std::string kind(to_string(o.kind));
                auto it = iface_written_.find(o.ifindex);
                if (it == iface_written_.end() || it->second.first != o.name ||
                    it->second.second != kind) {
                    // Registration is a write like any other, and on a full disk
                    // it fails like any other. It must not abandon the tick: the
                    // live frames are meant to keep flowing (C4 residual), and
                    // this runs long before the broadcast. Not memoised on
                    // failure, so it is retried rather than assumed written.
                    try {
                        store_.upsert_iface(o.ifindex, o.name, kind);
                        iface_written_[o.ifindex] = {o.name, kind};
                        ++iface_writes_;
                    } catch (const std::exception& e) {
                        log_tick_error(e.what());
                    }
                }
            }

            if (!o.has_delta) continue;
            if (!counts_toward_totals(o.kind)) continue;
            if (o.d_rx == 0 && o.d_tx == 0) continue;
            deltas.push_back({o.ifindex, o.d_rx, o.d_tx});
        }

        // Exec/exit identity events, ahead of the byte drain so a process that
        // exited during this interval is still resolvable when its final bytes
        // are attributed below.
        for (const auto& ev : collector_.drain_events()) {
            if (ev.type == ProcEvent::Type::Exec) {
                resolver_.note_exec(ev.tgid, ev.exe);
            } else {
                resolver_.note_exit(ev.tgid);
            }
        }

        // Per-application deltas from the BPF map. drain_app() returns nothing when
        // the collector is degraded, so this is a no-op if BPF never attached.
        std::vector<AppDelta> app_deltas;
        for (const auto& [key, val] : collector_.drain_app()) {
            const std::string exe_path = resolver_.exe_for(key.tgid);

            // The id is looked up once per executable per daemon lifetime. Two
            // consequences worth stating rather than discovering: cgroup_hint
            // keeps the cgroup of the FIRST process seen for that executable
            // instead of the most recent - both are arbitrary, and it is a hint -
            // and a renamed application (app_name_from_exe changing, as U3's fix
            // did) is rewritten on the next daemon start rather than the next
            // tick, since the memo starts empty.
            std::int64_t app_id;
            auto cached = app_ids_.find(exe_path);
            if (cached != app_ids_.end()) {
                app_id = cached->second;
            } else {
                const std::string name = app_name_from_exe(exe_path);
                const std::string cg = resolver_.cgroup_path(key.tgid, key.cgroup_id);
                try {
                    app_id = store_.upsert_app(exe_path, name, cg);
                } catch (const std::exception& e) {
                    // An application first seen while the database is unwritable
                    // has no id, and a delta cannot be retained without one - so
                    // THIS tick's bytes for THIS application are lost, and that
                    // is the one loss the retry buffer cannot cover. Applications
                    // already in the memo are unaffected, which is nearly all of
                    // them by the time a disk fills.
                    log_tick_error(e.what());
                    continue;
                }
                app_ids_.emplace(exe_path, app_id);
                ++app_writes_;
            }

            // The BPF hooks fire for every process on the machine in every
            // network namespace, but `ifaces`/prev_ only ever holds ifindexes
            // this daemon has observed via rtnetlink in its own netns. An
            // ifindex from a container's netns either violates the app_samples
            // foreign key (crash) or, worse, collides with an unrelated host
            // interface of the same number (silent misattribution). Clamp to
            // the sentinel unless this daemon has actually seen that ifindex.
            const int ifx = prev_.count(static_cast<int>(key.ifindex))
                                ? static_cast<int>(key.ifindex)
                                : 0;
            app_deltas.push_back({app_id, ifx, val.rx, val.tx});
        }

        // Drop identities for tgids that exited before this drain - now that the
        // drain above has had a chance to attribute their last bytes.
        resolver_.sweep_exited();

        // Anything a previous tick drained but could not write goes in with this
        // tick's data (I6). Merged before the write, so one failure does not
        // fork the retry path from the normal one.
        if (!pending_ifaces_.empty() || !pending_apps_.empty()) {
            deltas.insert(deltas.end(), pending_ifaces_.begin(), pending_ifaces_.end());
            app_deltas.insert(app_deltas.end(), pending_apps_.begin(), pending_apps_.end());
            pending_ifaces_.clear();
            pending_apps_.clear();
        }

        // Written once per tick so a tick is one transaction.
        //
        // A write failure does NOT abandon the tick (C4 residual). The stated
        // goal is to keep serving live frames while the disk is full, and the
        // catch that wrapped this whole body skipped the broadcast along with
        // everything else.
        //
        // Broadcasting unwritten bytes is only honest because I6 retains them:
        // they are held and land in a later tick, so the live graph is showing
        // traffic that WILL be in the history rather than traffic that will
        // never appear. Without the retry buffer this would be a lie, and an
        // earlier version of this comment argued exactly that - the two changes
        // have to be read together.
        bool stored = true;
        if (!deltas.empty() || !app_deltas.empty()) {
            try {
                store_.write_samples(now, deltas, app_deltas);
            } catch (const std::exception& e) {
                // drain_app() already deleted these from the kernel map, so
                // dropping them here would destroy them outright.
                retain_unwritten(deltas, app_deltas);
                stored = false;
                log_tick_error(e.what());
            }
        }

        // Rolling up a database that just refused a write only produces a
        // second failure with the same cause; the next tick retries both.
        if (stored && now_mono - last_rollup_mono_ >= std::chrono::seconds(60)) {
            last_rollup_mono_ = now_mono;
            rollup_tier(store_.handle(), kRes1s, kRes1m, now);
            rollup_tier(store_.handle(), kRes1m, kRes1h, now);
            rollup_tier(store_.handle(), kRes1h, kRes1d, now);
            enforce_retention(store_.handle(), now);

            // incremental_vacuum only needs to run once a day, alongside the
            // daily rollup - not every minute (see I8).
            const std::int64_t today = bucket_start(now, kRes1d);
            if (today != last_vacuum_day_) {
                last_vacuum_day_ = today;
                vacuum_incremental(store_.handle());
            }
        }

        if (control_) {
            std::vector<FrameIface> fi;
            for (const auto& d : deltas) fi.push_back({d.ifindex, d.rx, d.tx});
            std::vector<FrameApp> fa;
            for (const auto& d : app_deltas) {
                fa.push_back({d.app_id, d.rx, d.tx, d.ifindex});
            }
            control_->broadcast(frame(encode_sample(now, fi, fa)));
        }
    } catch (const std::exception& e) {
        log_tick_error(e.what());
    }
}

int run_daemon(const std::string& db_path, const std::string& socket_path) {
    std::signal(SIGTERM, on_signal);
    std::signal(SIGINT, on_signal);

    try {
        Store store(db_path);
        store.migrate();
        DaemonLoop loop(store, "/sys/class/net");
        loop.start_collector();  // degrades to interface-only totals on failure

        UnixSocketControl control(socket_path);
        if (!control.start()) {
            std::fprintf(stderr, "bandsightd: control socket unavailable; "
                                 "continuing without live frames\n");
        }
        loop.attach_control(&control);

        // Hello is built once from the machine's current interface set (not the
        // delta-tracking observe_ifaces(), which mutates DaemonLoop's baseline
        // map) and handed to every client that connects from here on.
        std::vector<HelloIface> hello_ifaces;
        try {
            for (const auto& s : read_iface_stats()) {
                hello_ifaces.push_back(
                    {s.ifindex, s.name, std::string(to_string(classify_iface(s.name)))});
            }
        } catch (const std::exception& e) {
            std::fprintf(stderr, "bandsightd: hello iface enumeration failed: %s\n", e.what());
        }
        control.set_hello(frame(encode_hello(db_path, hello_ifaces)));

        std::fprintf(stderr, "bandsightd: started, db=%s\n", db_path.c_str());

        // Cadence is paced off a monotonic deadline, not off how long tick() takes
        // or how often poll() returns early. Without this, either an unavailable
        // socket (poll() returning instantly) or a client that writes steadily
        // (poll() waking on every read) would drive tick() far faster than 1 Hz -
        // see C3. control.poll() is handed only the time left until the next
        // deadline, so it still blocks and costs no CPU while idle.
        auto next = std::chrono::steady_clock::now();
        while (!loop.stopping()) {
            const auto now_mono = std::chrono::steady_clock::now();
            if (now_mono >= next) {
                loop.tick(static_cast<std::int64_t>(std::time(nullptr)));
                next += std::chrono::seconds(1);
                // Fell behind (a long tick, or the process was suspended): skip
                // the missed deadlines rather than bursting to catch up.
                if (next < now_mono) next = now_mono + std::chrono::seconds(1);
            }
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                                        next - std::chrono::steady_clock::now())
                                        .count();
            control.poll(static_cast<int>(remaining > 0 ? remaining : 0));
        }
        std::fprintf(stderr, "bandsightd: stopping\n");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "bandsightd: fatal: %s\n", e.what());
        return 1;
    }
}

}  // namespace bandsight
