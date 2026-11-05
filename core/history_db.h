// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace bandsight::core {

// Every history query the GUI and the command-line client make, against the
// daemon's database (spec §3), on the plain sqlite3 C API: no Qt, so a headless
// machine can read its own history.
//
// The handle is read-only (SQLITE_OPEN_READONLY): writes go through the daemon,
// never through a reader. Reading while the daemon writes is what WAL is for; a
// reader needs read access to the database and its -wal and -shm files, which
// the daemon grants to group "bandsight".
//
// Byte counts are what the daemon stored: rx is download, tx is upload.

struct IfaceTotal {
    int id = 0;            // the newest ifindex this name has had
    std::string name;
    std::string kind;      // physical, wifi, loopback, vpn, virtual, unknown...
    std::uint64_t rx = 0;
    std::uint64_t tx = 0;
    std::vector<int> ids;  // every ifindex the name has had (a USB adapter gets a new one per plug-in)
};

struct AppTotal {
    int id = 0;
    std::string exe;   // the identity the daemon recorded
    std::string name;  // friendlier label; may be empty (see app_labels.h)
    std::uint64_t rx = 0;
    std::uint64_t tx = 0;
};

struct AppHostTotal {
    int app_id = 0;
    std::string exe;
    std::string app_name;
    int host_id = 0;
    std::string ip;        // empty: no peer address (unconnected UDP)
    std::string hostname;  // empty when no name was seen
    std::uint64_t rx = 0;
    std::uint64_t tx = 0;
};

// proto 6 (TCP) or 17 (UDP) and the service port the daemon chose, 0 for
// "other" (see daemon/traffic_type.h).
struct TypeTotal {
    int proto = 0;
    int port = 0;
    std::uint64_t rx = 0;
    std::uint64_t tx = 0;
};

struct SeriesPoint {
    std::int64_t ts = 0;
    std::uint64_t rx = 0;
    std::uint64_t tx = 0;
};

// collected=false: the database predates host collection (no iface_id column
// on app_host_samples) - an old daemon, not an idle network. since: when host
// recording began, if that is inside the period (0 otherwise) - app history is
// older than host history, so the two cannot be compared before it. ok=false:
// the query failed and pairs is empty.
struct HostPairs {
    std::vector<AppHostTotal> pairs;
    bool collected = false;
    std::int64_t since = 0;
    bool ok = true;
};

// The same, for type_samples: collected=false when that table does not exist.
struct TypeTotals {
    std::vector<TypeTotal> totals;
    bool collected = false;
    std::int64_t since = 0;
    bool ok = true;
};

// Usage against the data cap. cap == 0 means no cap is configured.
struct Quota {
    std::uint64_t used = 0;
    std::uint64_t cap = 0;
    std::uint64_t projected = 0;
    std::int64_t cycle_start = 0;
    std::int64_t cycle_end = 0;
    int cycle_day = 1;
};

// A half-open-or-closed span of bucket timestamps, as the queries take it.
struct Span {
    std::int64_t from = 0;
    std::int64_t to = 0;
};

// The bucket span a "last range_s seconds at resolution res" window covers:
// from the bucket holding now - range_s to one past the newest CLOSED bucket.
// rollup_tier only writes buckets strictly before bucket_start(now), so that is
// exactly where the data ends. Uses the daemon's own bucket_start, so the
// res=3600 tier lands on local hours in every zone.
Span window_span(int res, std::int64_t range_s, std::int64_t now);

// Host and type rows are minute-grained at best (no res=1 tier), so the seconds
// tier maps to minutes. Longer tiers are unchanged.
int host_res(int res);

// Why open() failed, for a message a person can act on.
enum class OpenError { None, NotFound, Permission, NotADatabase, Other };

class HistoryDb {
public:
    HistoryDb() = default;
    ~HistoryDb();
    HistoryDb(const HistoryDb&) = delete;
    HistoryDb& operator=(const HistoryDb&) = delete;

    // Read-only. Closes any previous handle first. On failure, error() holds
    // SQLite's message and open_error() the kind of failure.
    //
    // SQLite opens lazily: a file that is not a database, or a -shm this user
    // cannot read, only fails on the first read. Check::Read reads the schema
    // so that a successful open() means "readable"; Check::None is SQLite's own
    // behaviour, which the GUI has always had (its first query reports it).
    enum class Check { Read, None };
    bool open(const std::string& path, Check check = Check::Read);
    void close();
    bool is_open() const { return db_ != nullptr; }
    const std::string& error() const { return error_; }
    OpenError open_error() const { return open_error_; }

    // SQLite's message for the last failed query ("" when it succeeded).
    const std::string& last_query_error() const { return query_error_; }

    // One point per bucket in [start, end), zero-filled: an idle bucket is a zero
    // observation, never a gap (spec §4.3). Summed over `ifaces`. ok=false when
    // the query failed, in which case every point is zero.
    std::vector<SeriesPoint> series(int res, std::int64_t start, std::int64_t end,
                                    const std::vector<int>& ifaces, bool* ok = nullptr);

    // Every interface the database knows (except row 0), grouped by name, with
    // its bytes over [from, to] at tier res; zero for one that moved nothing.
    // nullopt when the query failed.
    std::optional<std::vector<IfaceTotal>> iface_totals(std::int64_t from, std::int64_t to,
                                                        int res);

    // Applications that moved anything over [from, to], largest first. Only the
    // interface kinds that are measured (physical, wifi) plus row 0 ("unknown",
    // route-less sockets) count - see history_db.cpp. nullopt on failure.
    std::optional<std::vector<AppTotal>> app_totals(std::int64_t from, std::int64_t to,
                                                    int res);

    // (app, host) pairs. [from, to] is the period asked for; data_end is where
    // the newest row's data actually ends. A seconds-tier period is widened to
    // whole minutes and prorated to its own length.
    HostPairs host_pairs(std::int64_t from, std::int64_t to, std::int64_t data_end, int res);
    // A "last range_s seconds" window: to now, not to the last closed bucket,
    // since the daemon writes the current minute live.
    HostPairs host_pairs_window(int res, std::int64_t range_s, std::int64_t now);
    // An explicit [from, to]: its last minute row holds that whole minute -
    // unless it is the live minute, which only holds up to now.
    HostPairs host_pairs_range(std::int64_t from, std::int64_t to, int res, std::int64_t now);

    // Bytes per (proto, port), with the same tiers, widening and proration as
    // the host pairs and the same interface filter as the apps.
    TypeTotals type_totals(std::int64_t from, std::int64_t to, std::int64_t data_end, int res);
    TypeTotals type_totals_window(int res, std::int64_t range_s, std::int64_t now);
    TypeTotals type_totals_range(std::int64_t from, std::int64_t to, int res, std::int64_t now);

    // The cycle containing now (from config cycle_start_day), the cap (config
    // cap_bytes), what the measured interfaces moved in it so far, and the
    // linear projection to its end.
    Quota quota(std::int64_t now);

    // A row of the config table; nullopt when absent or unreadable.
    std::optional<std::string> config_value(const std::string& key);

private:
    std::uint64_t sum_over(int res, std::int64_t from, std::int64_t to, std::uint64_t& tx_out);
    void fail();   // records SQLite's message for last_query_error()

    sqlite3* db_ = nullptr;
    std::string error_;
    std::string query_error_;
    OpenError open_error_ = OpenError::None;
};

// Strict integer parsing for config values: optional surrounding whitespace, an
// optional sign ('-' only for the signed form), digits, nothing else.
std::optional<std::int64_t> parse_i64(const std::string& s);
std::optional<std::uint64_t> parse_u64(const std::string& s);

}  // namespace bandsight::core
