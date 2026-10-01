// SPDX-License-Identifier: GPL-3.0-or-later
#include "rollup.h"

#include <stdexcept>
#include <string>

#include "time_buckets.h"

namespace bandsight {
namespace {

void exec(sqlite3* db, const std::string& sql) {
    char* err = nullptr;
    if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        const std::string msg = err ? err : "exec failed";
        sqlite3_free(err);
        throw std::runtime_error("rollup: " + msg + " [" + sql + "]");
    }
}

std::int64_t watermark(sqlite3* db, int res) {
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db, "SELECT last_ts FROM rollup_state WHERE res=?", -1, &st, nullptr);
    sqlite3_bind_int(st, 1, res);
    std::int64_t v = 0;
    if (sqlite3_step(st) == SQLITE_ROW) v = sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    return v;
}

std::int64_t earliest_src_ts(sqlite3* db, int src_res) {
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db,
        "SELECT MIN(t) FROM ("
        "  SELECT MIN(ts) AS t FROM iface_samples WHERE res=?1"
        "  UNION ALL SELECT MIN(ts) AS t FROM app_samples WHERE res=?1)",
        -1, &st, nullptr);
    sqlite3_bind_int(st, 1, src_res);
    std::int64_t v = 0;
    if (sqlite3_step(st) == SQLITE_ROW && sqlite3_column_type(st, 0) != SQLITE_NULL) {
        v = sqlite3_column_int64(st, 0);
    }
    sqlite3_finalize(st);
    return v;
}

// Rolls [from, to) of src_res into a single dst_res bucket stamped `bucket`.
void roll_one_bucket(sqlite3* db, int src_res, int dst_res, std::int64_t bucket,
                     std::int64_t from, std::int64_t to) {
    const std::string b = std::to_string(bucket);
    const std::string s = std::to_string(src_res);
    const std::string d = std::to_string(dst_res);
    const std::string lo = std::to_string(from);
    const std::string hi = std::to_string(to);

    // Delete first so a re-run replaces rather than doubles.
    exec(db, "DELETE FROM iface_samples WHERE res=" + d + " AND ts=" + b);
    exec(db,
         "INSERT INTO iface_samples(res,ts,iface_id,rx,tx) "
         "SELECT " + d + "," + b + ",iface_id,SUM(rx),SUM(tx) FROM iface_samples "
         "WHERE res=" + s + " AND ts>=" + lo + " AND ts<" + hi + " GROUP BY iface_id");

    exec(db, "DELETE FROM app_samples WHERE res=" + d + " AND ts=" + b);
    exec(db,
         "INSERT INTO app_samples(res,ts,app_id,iface_id,rx,tx) "
         "SELECT " + d + "," + b + ",app_id,iface_id,SUM(rx),SUM(tx) FROM app_samples "
         "WHERE res=" + s + " AND ts>=" + lo + " AND ts<" + hi +
         " GROUP BY app_id,iface_id");

    // app_host_samples only exists at the 3600 and 86400 tiers.
    if (dst_res == kRes1h || dst_res == kRes1d) {
        exec(db, "DELETE FROM app_host_samples WHERE res=" + d + " AND ts=" + b);
        exec(db,
             "INSERT INTO app_host_samples(res,ts,app_id,host_id,rx,tx) "
             "SELECT " + d + "," + b + ",app_id,host_id,SUM(rx),SUM(tx) FROM app_host_samples "
             "WHERE res=" + s + " AND ts>=" + lo + " AND ts<" + hi +
             " GROUP BY app_id,host_id");
    }
}

}  // namespace

void rollup_tier(sqlite3* db, int src_res, int dst_res, std::int64_t now) {
    std::int64_t cursor = watermark(db, dst_res);
    if (cursor == 0) {
        const std::int64_t first = earliest_src_ts(db, src_res);
        if (first == 0) return;  // nothing to roll
        cursor = bucket_start(first, dst_res);
    }

    // Only close buckets that have fully elapsed.
    const std::int64_t current = bucket_start(now, dst_res);

    exec(db, "BEGIN");
    try {
        std::int64_t last_done = cursor;
        while (cursor < current) {
            const std::int64_t next = bucket_next(cursor, dst_res);
            roll_one_bucket(db, src_res, dst_res, cursor, cursor, next);
            last_done = next;
            cursor = next;
        }
        exec(db, "INSERT INTO rollup_state(res,last_ts) VALUES(" + std::to_string(dst_res) +
                     "," + std::to_string(last_done) + ") "
                     "ON CONFLICT(res) DO UPDATE SET last_ts=excluded.last_ts");
    } catch (...) {
        exec(db, "ROLLBACK");
        throw;
    }
    exec(db, "COMMIT");
}

void enforce_retention(sqlite3* db, std::int64_t now) {
    struct Tier { int res; std::int64_t keep_seconds; };
    // 86400 is absent on purpose: the daily tier is kept forever.
    static constexpr Tier kTiers[] = {
        {kRes1s, 3600},
        {kRes1m, 30LL * 86400},
        {kRes1h, 365LL * 86400},
    };

    exec(db, "BEGIN");
    try {
        for (const auto& t : kTiers) {
            const std::string cutoff = std::to_string(now - t.keep_seconds);
            const std::string res = std::to_string(t.res);
            exec(db, "DELETE FROM iface_samples WHERE res=" + res + " AND ts<" + cutoff);
            exec(db, "DELETE FROM app_samples WHERE res=" + res + " AND ts<" + cutoff);
        }
        // app_host_samples: 3600 tier keeps 90 days, 86400 forever.
        exec(db, "DELETE FROM app_host_samples WHERE res=" + std::to_string(kRes1h) +
                     " AND ts<" + std::to_string(now - 90LL * 86400));
    } catch (...) {
        exec(db, "ROLLBACK");
        throw;
    }
    exec(db, "COMMIT");
}

void vacuum_incremental(sqlite3* db) { exec(db, "PRAGMA incremental_vacuum"); }

}  // namespace bandsight
