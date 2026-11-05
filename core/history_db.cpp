// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/history_db.h"

#include <sqlite3.h>
#include <sys/stat.h>

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <unordered_map>

#include "core/cycle.h"
#include "time_buckets.h"

namespace bandsight::core {

namespace {

// A prepared statement that finalizes itself.
class Stmt {
public:
    Stmt(sqlite3* db, const std::string& sql) {
        if (db) sqlite3_prepare_v2(db, sql.c_str(), int(sql.size()), &s_, nullptr);
    }
    ~Stmt() { sqlite3_finalize(s_); }
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;

    bool ok() const { return s_ != nullptr; }
    Stmt& bind(std::int64_t v) {
        if (s_) sqlite3_bind_int64(s_, ++arg_, v);
        return *this;
    }
    Stmt& bind(const std::string& v) {
        if (s_) sqlite3_bind_text(s_, ++arg_, v.c_str(), int(v.size()), SQLITE_TRANSIENT);
        return *this;
    }
    // SQLITE_ROW -> true; SQLITE_DONE or an error -> false (done() tells which).
    bool step() {
        if (!s_) return false;
        rc_ = sqlite3_step(s_);
        return rc_ == SQLITE_ROW;
    }
    bool done() const { return s_ && rc_ == SQLITE_DONE; }

    std::int64_t i64(int c) const { return sqlite3_column_int64(s_, c); }
    std::uint64_t u64(int c) const { return std::uint64_t(sqlite3_column_int64(s_, c)); }
    int i32(int c) const { return sqlite3_column_int(s_, c); }
    bool null(int c) const { return sqlite3_column_type(s_, c) == SQLITE_NULL; }
    std::string text(int c) const {
        const auto* p = sqlite3_column_text(s_, c);
        return p ? std::string(reinterpret_cast<const char*>(p),
                               std::size_t(sqlite3_column_bytes(s_, c)))
                 : std::string();
    }

private:
    sqlite3_stmt* s_ = nullptr;
    int arg_ = 0;
    int rc_ = SQLITE_OK;
};

std::string in_list(const std::vector<int>& ids) {   // ints only - no injection surface
    std::string l;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (i) l += ',';
        l += std::to_string(ids[i]);
    }
    return l;
}

// The interface kinds a breakdown counts, spelled out in SQL rather than
// filtered in C++ so the database does the work. Restricted for the same reason
// the Interfaces tab marks the rest "not measured" (spec §2.2): loopback and
// tunnel bytes are either counted again on the physical interface underneath or
// never leave the machine. Summing them made a python process that talks to
// itself the largest consumer on the system by three orders of magnitude.
//
// Row 0 ("unknown") IS included. Dropping it hid most of the traffic of
// route-less sockets - unconnected UDP: WebRTC calls, tailscaled's WireGuard,
// some QUIC - which the headline counted from the interface counters. The
// daemon credits loopback datagrams to lo even without a route, which is what
// makes this safe from the loopback problem above. Container sockets are stored
// under their own "other-netns" row (kind virtual) and stay out.
constexpr const char* kBreakdownKinds = "('physical','wifi','unknown')";

double proration(int qres, int res, std::int64_t from, std::int64_t to, std::int64_t start,
                 std::int64_t data_end) {
    // Seconds-tier periods read whole-minute rows, which span more than the
    // period (up to twice, on the 1-minute window). Unscaled, excluding one of
    // two equal hosts would remove most of the headline. Prorating assumes an
    // even rate within the span; it is an estimate, and only these short
    // periods need it - longer ones read the same tier as the apps.
    if (qres != res && data_end > start)
        return std::clamp(double(to - from) / double(data_end - start), 0.0, 1.0);
    return 1.0;
}

std::uint64_t scaled(std::uint64_t v, double scale) {
    return std::uint64_t(std::llround(double(v) * scale));
}

std::string trim(const std::string& s) {
    const auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; };
    std::size_t a = 0, b = s.size();
    while (a < b && ws(s[a])) ++a;
    while (b > a && ws(s[b - 1])) --b;
    return s.substr(a, b - a);
}

}  // namespace

std::optional<std::int64_t> parse_i64(const std::string& in) {
    const std::string s = trim(in);
    if (s.empty()) return std::nullopt;
    std::size_t i = (s[0] == '+' || s[0] == '-') ? 1 : 0;
    if (i == s.size()) return std::nullopt;
    for (std::size_t k = i; k < s.size(); ++k)
        if (s[k] < '0' || s[k] > '9') return std::nullopt;
    errno = 0;
    const long long v = std::strtoll(s.c_str(), nullptr, 10);
    if (errno == ERANGE) return std::nullopt;
    return std::int64_t(v);
}

std::optional<std::uint64_t> parse_u64(const std::string& in) {
    const std::string s = trim(in);
    if (s.empty()) return std::nullopt;
    const std::size_t i = s[0] == '+' ? 1 : 0;
    if (i == s.size()) return std::nullopt;
    for (std::size_t k = i; k < s.size(); ++k)
        if (s[k] < '0' || s[k] > '9') return std::nullopt;
    errno = 0;
    const unsigned long long v = std::strtoull(s.c_str() + i, nullptr, 10);
    if (errno == ERANGE) return std::nullopt;
    return std::uint64_t(v);
}

Span window_span(int res, std::int64_t range_s, std::int64_t now) {
    return {bandsight::bucket_start(now - range_s, res), bandsight::bucket_start(now, res)};
}

int host_res(int res) { return res == 1 ? 60 : res; }

HistoryDb::~HistoryDb() { close(); }

void HistoryDb::close() {
    if (db_) sqlite3_close_v2(db_);
    db_ = nullptr;
}

bool HistoryDb::open(const std::string& path, Check check) {
    close();
    error_.clear();
    open_error_ = OpenError::None;

    sqlite3* db = nullptr;
    const int rc = sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
    if (rc == SQLITE_OK) {
        // Matches the Qt SQLite driver's default, which the GUI used before:
        // a reader that meets the daemon mid-checkpoint waits instead of failing.
        sqlite3_busy_timeout(db, 5000);
        if (check == Check::None) {
            db_ = db;
            return true;
        }
        Stmt probe(db, "SELECT COUNT(*) FROM sqlite_master");
        if (probe.ok() && probe.step()) {
            db_ = db;
            return true;
        }
    }
    error_ = db ? sqlite3_errmsg(db) : "out of memory";
    const int code = db ? (sqlite3_extended_errcode(db) & 0xff) : SQLITE_NOMEM;
    if (db) sqlite3_close_v2(db);

    struct stat st {};
    if (::stat(path.c_str(), &st) != 0) {
        open_error_ = errno == EACCES ? OpenError::Permission : OpenError::NotFound;
    } else if (code == SQLITE_NOTADB) {
        open_error_ = OpenError::NotADatabase;
    } else if (code == SQLITE_CANTOPEN || code == SQLITE_PERM || code == SQLITE_READONLY ||
               code == SQLITE_AUTH) {
        // The file exists but this user cannot open it, or cannot open the -shm
        // or -wal beside it - which is the same fix.
        open_error_ = OpenError::Permission;
    } else {
        open_error_ = OpenError::Other;
    }
    return false;
}

void HistoryDb::fail() {
    query_error_ = db_ ? sqlite3_errmsg(db_) : "database not open";
}

std::vector<SeriesPoint> HistoryDb::series(int res, std::int64_t start, std::int64_t end,
                                           const std::vector<int>& ifaces, bool* ok) {
    query_error_.clear();
    std::unordered_map<std::int64_t, SeriesPoint> rows;
    Stmt q(db_, "SELECT ts, SUM(rx), SUM(tx) FROM iface_samples "
                "WHERE res=? AND ts>=? AND iface_id IN (" + in_list(ifaces) + ") GROUP BY ts");
    q.bind(res).bind(start);
    while (q.step()) rows[q.i64(0)] = {q.i64(0), q.u64(1), q.u64(2)};
    const bool good = q.done();
    if (!good) fail();
    if (ok) *ok = good;

    std::vector<SeriesPoint> out;
    if (end > start && res > 0) out.reserve(std::size_t((end - start) / res));
    for (std::int64_t ts = start; ts < end; ts = bandsight::bucket_next(ts, res)) {
        const auto it = rows.find(ts);
        out.push_back(it != rows.end() ? it->second : SeriesPoint{ts, 0, 0});
    }
    return out;
}

std::optional<std::vector<IfaceTotal>> HistoryDb::iface_totals(std::int64_t from,
                                                               std::int64_t to, int res) {
    query_error_.clear();
    // Grouped by name, not ifindex: a replugged adapter is one interface under
    // several ifindexes. The kind is the newest ifindex's - SQLite takes a bare
    // column from the row that MAX() picked.
    Stmt q(db_,
        "SELECT MAX(i.id), i.name, i.kind, "
        "COALESCE(SUM(s.rx),0), COALESCE(SUM(s.tx),0), GROUP_CONCAT(DISTINCT i.id) "
        "FROM ifaces i LEFT JOIN iface_samples s "
        "  ON s.iface_id = i.id AND s.res=? AND s.ts>=? AND s.ts<=? "
        "WHERE i.id != 0 GROUP BY i.name ORDER BY i.name");
    q.bind(res).bind(from).bind(to);
    std::vector<IfaceTotal> out;
    while (q.step()) {
        IfaceTotal t;
        t.id = q.i32(0);
        t.name = q.text(1);
        t.kind = q.text(2);
        t.rx = q.u64(3);
        t.tx = q.u64(4);
        const std::string ids = q.text(5);
        std::size_t at = 0;
        for (;;) {
            const std::size_t comma = ids.find(',', at);
            t.ids.push_back(std::atoi(ids.substr(at, comma - at).c_str()));
            if (comma == std::string::npos) break;
            at = comma + 1;
        }
        out.push_back(std::move(t));
    }
    if (!q.done()) {
        fail();
        return std::nullopt;
    }
    return out;
}

// Unlike iface_totals this INNER JOINs on app_samples, and deliberately so: an
// interface with no samples is a fact worth showing ("not measured"), but the
// apps table accumulates every executable ever seen - 161 of them on the
// development machine - and listing the ones that moved nothing would bury the
// handful that did.
std::optional<std::vector<AppTotal>> HistoryDb::app_totals(std::int64_t from, std::int64_t to,
                                                           int res) {
    query_error_.clear();
    Stmt q(db_, std::string(
        "SELECT a.id, a.exe, a.name, SUM(s.rx), SUM(s.tx) "
        "FROM apps a JOIN app_samples s ON s.app_id = a.id "
        "JOIN ifaces i ON i.id = s.iface_id "
        "WHERE s.res=? AND s.ts>=? AND s.ts<=? AND i.kind IN ") + kBreakdownKinds + " "
        "GROUP BY a.id HAVING SUM(s.rx) + SUM(s.tx) > 0 "
        "ORDER BY SUM(s.rx) + SUM(s.tx) DESC");
    q.bind(res).bind(from).bind(to);
    std::vector<AppTotal> out;
    while (q.step()) out.push_back({q.i32(0), q.text(1), q.text(2), q.u64(3), q.u64(4)});
    if (!q.done()) {
        fail();
        return std::nullopt;
    }
    return out;
}

HostPairs HistoryDb::host_pairs_window(int res, std::int64_t range_s, std::int64_t now) {
    return host_pairs(now - range_s, now, now, res);
}

HostPairs HistoryDb::host_pairs_range(std::int64_t from, std::int64_t to, int res,
                                      std::int64_t now) {
    const int hres = host_res(res);
    const std::int64_t data_end = std::min<std::int64_t>(
        bandsight::bucket_next(bandsight::bucket_start(to, hres), hres), now);
    return host_pairs(from, to, std::max(data_end, to), res);
}

HostPairs HistoryDb::host_pairs(std::int64_t from, std::int64_t to, std::int64_t data_end,
                                int res) {
    query_error_.clear();
    HostPairs r;
    const int hres = host_res(res);
    const std::int64_t start = bandsight::bucket_start(from, hres);   // widen to whole buckets

    // Detected by column, not by version number: what matters is whether the
    // table this query reads has the shape it reads.
    {
        Stmt probe(db_, "SELECT COUNT(*) FROM pragma_table_info('app_host_samples') "
                        "WHERE name='iface_id'");
        r.collected = probe.step() && probe.i64(0) == 1;
    }
    if (!r.collected) return r;

    // Recording begins when a v3 daemon is installed; app history is older.
    {
        Stmt probe(db_, "SELECT MIN(ts) FROM app_host_samples");
        if (probe.step() && !probe.null(0)) {
            const std::int64_t first = probe.i64(0);
            if (first > start) r.since = first;
        }
    }

    const double scale = proration(hres, res, from, to, start, data_end);
    Stmt q(db_, std::string(
        "SELECT a.id, a.exe, a.name, h.id, h.ip, h.hostname, SUM(s.rx), SUM(s.tx) "
        "FROM app_host_samples s "
        "JOIN apps a ON a.id = s.app_id "
        "JOIN hosts h ON h.id = s.host_id "
        "JOIN ifaces i ON i.id = s.iface_id "
        "WHERE s.res=? AND s.ts>=? AND s.ts<=? AND i.kind IN ") + kBreakdownKinds + " "
        "GROUP BY a.id, h.id HAVING SUM(s.rx) + SUM(s.tx) > 0");
    q.bind(hres).bind(start).bind(to);
    while (q.step()) {
        r.pairs.push_back({q.i32(0), q.text(1), q.text(2), q.i32(3), q.text(4), q.text(5),
                           scaled(q.u64(6), scale), scaled(q.u64(7), scale)});
    }
    if (!q.done()) {
        fail();
        r.pairs.clear();
        r.ok = false;
    }
    return r;
}

TypeTotals HistoryDb::type_totals_window(int res, std::int64_t range_s, std::int64_t now) {
    return type_totals(now - range_s, now, now, res);
}

TypeTotals HistoryDb::type_totals_range(std::int64_t from, std::int64_t to, int res,
                                        std::int64_t now) {
    const int tres = host_res(res);
    const std::int64_t data_end = std::min<std::int64_t>(
        bandsight::bucket_next(bandsight::bucket_start(to, tres), tres), now);
    return type_totals(from, to, std::max(data_end, to), res);
}

TypeTotals HistoryDb::type_totals(std::int64_t from, std::int64_t to, std::int64_t data_end,
                                  int res) {
    query_error_.clear();
    TypeTotals r;
    const int tres = host_res(res);
    const std::int64_t start = bandsight::bucket_start(from, tres);

    {
        Stmt probe(db_, "SELECT COUNT(*) FROM sqlite_master WHERE type='table' "
                        "AND name='type_samples'");
        r.collected = probe.step() && probe.i64(0) == 1;
    }
    if (!r.collected) return r;

    // When recording began, as the daemon stored it on its first type write.
    // Not MIN(ts): rolled-up rows carry the start of their hour or day, which
    // reads up to a day early. The fallback, for a database written before the
    // key existed, is per tier so each MIN() is one primary-key seek.
    std::optional<std::int64_t> first;
    if (const auto v = config_value("types_since")) first = parse_i64(*v);
    if (!first) {
        Stmt probe(db_,
            "SELECT MIN(t) FROM ("
            "  SELECT MIN(ts) AS t FROM type_samples WHERE res=60"
            "  UNION ALL SELECT MIN(ts) AS t FROM type_samples WHERE res=3600"
            "  UNION ALL SELECT MIN(ts) AS t FROM type_samples WHERE res=86400)");
        if (probe.step() && !probe.null(0)) first = probe.i64(0);
    }
    if (first && *first > start) r.since = *first;

    const double scale = proration(tres, res, from, to, start, data_end);
    Stmt q(db_, std::string(
        "SELECT s.proto, s.port, SUM(s.rx), SUM(s.tx) "
        "FROM type_samples s JOIN ifaces i ON i.id = s.iface_id "
        "WHERE s.res=? AND s.ts>=? AND s.ts<=? AND i.kind IN ") + kBreakdownKinds + " "
        "GROUP BY s.proto, s.port HAVING SUM(s.rx) + SUM(s.tx) > 0");
    q.bind(tres).bind(start).bind(to);
    while (q.step())
        r.totals.push_back({q.i32(0), q.i32(1), scaled(q.u64(2), scale), scaled(q.u64(3), scale)});
    if (!q.done()) {
        fail();
        r.totals.clear();
        r.ok = false;
    }
    return r;
}

std::uint64_t HistoryDb::sum_over(int res, std::int64_t from, std::int64_t to,
                                  std::uint64_t& tx_out) {
    Stmt q(db_,
        "SELECT COALESCE(SUM(s.rx),0), COALESCE(SUM(s.tx),0) "
        "FROM iface_samples s JOIN ifaces i ON i.id = s.iface_id "
        "WHERE s.res=? AND s.ts>=? AND s.ts<? "
        "  AND i.kind IN ('physical','wifi')");
    q.bind(res).bind(from).bind(to);
    if (!q.step()) return 0;
    tx_out += q.u64(1);
    return q.u64(0);
}

// Usage against the data cap (spec §7). Two tiers, not one, and that is the
// whole difficulty: rollup_tier only writes CLOSED buckets, so the res=86400
// tier's newest row is up to two days old. Summing dailies alone would
// under-report a monthly cap by up to 7%, always in the direction of claiming
// more headroom than exists, which is the one direction a cap indicator must
// never err in.
//
// So: whole closed days from res=86400, then everything after the last closed
// day from res=60, which is retained for 30 days and therefore always covers the
// tail. The boundary is taken from the data (MAX(ts) actually present) rather
// than from the clock, so a lagging or stalled rollup shifts the split instead
// of opening a hole.
Quota HistoryDb::quota(std::int64_t now) {
    query_error_.clear();
    Quota info;
    {
        const auto v = config_value("cycle_start_day");
        const auto d = v ? parse_i64(*v) : std::nullopt;
        info.cycle_day = d && *d >= INT_MIN && *d <= INT_MAX ? int(*d) : 1;
    }
    {
        const auto v = config_value("cap_bytes");
        const auto c = v ? parse_u64(*v) : std::nullopt;
        info.cap = c ? *c : 0;
    }

    const Cycle c = cycle_for(now, info.cycle_day);
    info.cycle_start = c.start;
    info.cycle_end = c.end;

    std::int64_t boundary = c.start;
    {
        Stmt q(db_, "SELECT MAX(ts) FROM iface_samples WHERE res=86400 AND ts>=? AND ts<?");
        q.bind(c.start).bind(now);
        // The day after the last closed one, by the daemon's own bucket rule:
        // + 86400 lands an hour early on a 25-hour day, counting that hour
        // twice, and an hour late on a 23-hour day, missing it.
        if (q.step() && !q.null(0)) boundary = bandsight::bucket_next(q.i64(0), 86400);
    }

    std::uint64_t tx = 0;
    std::uint64_t rx = sum_over(86400, c.start, boundary, tx);
    rx += sum_over(60, boundary, now + 1, tx);

    // A cap counts what crossed the link, in both directions: an ISP does not
    // bill upload separately.
    info.used = rx + tx;
    info.projected = project_to_cycle_end(info.used, c, now);
    return info;
}

std::optional<std::string> HistoryDb::config_value(const std::string& key) {
    Stmt q(db_, "SELECT value FROM config WHERE key=?");
    q.bind(key);
    if (q.step()) return q.text(0);
    return std::nullopt;
}

}  // namespace bandsight::core
