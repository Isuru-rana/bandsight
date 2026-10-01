// SPDX-License-Identifier: GPL-3.0-or-later
#include "store.h"

#include <sys/stat.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace bandsight {
namespace {

// v2: dropped the UNIQUE constraint on ifaces.name. The ifindex (id) is the
// real key; interface names are not unique over time (wg-quick down/up,
// tailscaled restart, docker0 recreation, a USB tether replug all reuse a
// name at a new ifindex), and upsert_iface only handles ON CONFLICT(id). A
// name collision on the old schema aborted the insert and crashed the daemon.
constexpr int kSchemaVersion = 2;

const char* kSchema = R"SQL(
CREATE TABLE IF NOT EXISTS apps(
  id INTEGER PRIMARY KEY,
  exe TEXT NOT NULL UNIQUE,
  name TEXT,
  cgroup_hint TEXT
);
CREATE TABLE IF NOT EXISTS ifaces(
  id INTEGER PRIMARY KEY,
  name TEXT NOT NULL,
  kind TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS hosts(
  id INTEGER PRIMARY KEY,
  ip TEXT NOT NULL UNIQUE,
  hostname TEXT,
  first_seen INTEGER,
  last_seen INTEGER
);
CREATE TABLE IF NOT EXISTS app_samples(
  res INTEGER NOT NULL,
  ts INTEGER NOT NULL,
  app_id INTEGER NOT NULL REFERENCES apps(id),
  iface_id INTEGER NOT NULL REFERENCES ifaces(id),
  rx INTEGER NOT NULL,
  tx INTEGER NOT NULL,
  PRIMARY KEY(res, ts, app_id, iface_id)
) WITHOUT ROWID;
CREATE TABLE IF NOT EXISTS iface_samples(
  res INTEGER NOT NULL,
  ts INTEGER NOT NULL,
  iface_id INTEGER NOT NULL REFERENCES ifaces(id),
  rx INTEGER NOT NULL,
  tx INTEGER NOT NULL,
  PRIMARY KEY(res, ts, iface_id)
) WITHOUT ROWID;
CREATE TABLE IF NOT EXISTS app_host_samples(
  res INTEGER NOT NULL,
  ts INTEGER NOT NULL,
  app_id INTEGER NOT NULL REFERENCES apps(id),
  host_id INTEGER NOT NULL REFERENCES hosts(id),
  rx INTEGER NOT NULL,
  tx INTEGER NOT NULL,
  PRIMARY KEY(res, ts, app_id, host_id)
) WITHOUT ROWID;
CREATE TABLE IF NOT EXISTS rollup_state(res INTEGER PRIMARY KEY, last_ts INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS config(key TEXT PRIMARY KEY, value TEXT NOT NULL);
)SQL";

}  // namespace

Store::Store(const std::string& path) {
    if (sqlite3_open_v2(path.c_str(), &db_,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        const std::string msg = db_ ? sqlite3_errmsg(db_) : "open failed";
        sqlite3_close(db_);
        throw std::runtime_error("Store: " + msg);
    }
    // Group members (the GUI) must be able to run read-only WAL recovery after
    // an unclean daemon stop, which needs write access to -wal/-shm. SQLite
    // creates sidecars with the db file's mode, so fixing the db here covers
    // fresh files; the explicit sidecar chmods heal existing deployments.
    // ENOENT on a sidecar is expected (they appear lazily, on first write) and
    // must stay silent; any other failure is logged, matching
    // UnixSocketControl::start()'s chmod handling.
    if (path != ":memory:") {
        for (const std::string& p : {path, path + "-wal", path + "-shm"}) {
            if (::chmod(p.c_str(), 0660) != 0 && errno != ENOENT) {
                std::fprintf(stderr,
                             "bandsightd: chmod(%s, 0660) failed: %s; group readers may not be "
                             "able to recover the WAL\n",
                             p.c_str(), std::strerror(errno));
            }
        }
    }
    // auto_vacuum only takes effect on a database with no tables yet, so it
    // must run before migrate() creates the schema. incremental_vacuum (run
    // daily alongside the rollup, see rollup.cpp) is a silent no-op without
    // this having been set here first.
    exec("PRAGMA auto_vacuum=INCREMENTAL");
    // WAL is a no-op for :memory: but harmless. foreign_keys is per-connection and
    // must be set here, not in the schema.
    exec("PRAGMA journal_mode=WAL");
    exec("PRAGMA synchronous=NORMAL");
    exec("PRAGMA foreign_keys=ON");
}

Store::~Store() { sqlite3_close(db_); }

void Store::exec(const std::string& sql) {
    char* err = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        const std::string msg = err ? err : "exec failed";
        sqlite3_free(err);
        throw std::runtime_error("Store::exec: " + msg + " [" + sql + "]");
    }
}

void Store::migrate() {
    exec("BEGIN");
    exec(kSchema);
    exec("INSERT OR IGNORE INTO ifaces(id, name, kind) VALUES(0, 'unknown', 'unknown')");
    exec("INSERT OR IGNORE INTO config(key, value) VALUES('schema_version', '" +
         std::to_string(kSchemaVersion) + "')");
    exec("COMMIT");

    // INSERT OR IGNORE above means an existing database keeps whatever version
    // it already had, so a mismatch reaches this point silently and only shows
    // up later as an obscure failure - a v1 database still carries the UNIQUE
    // index on ifaces.name and crash-loops the daemon on an interface rename
    // (C1 residual).
    //
    // No migration is written, deliberately: v1 was never deployed and nothing
    // in the wild carries it, so a table rebuild here would be speculative code
    // that has never run against a real database of the shape it claims to fix.
    // What IS written is the refusal, because it costs ten lines and turns a
    // crash into a sentence. The same check catches the opposite case, which
    // will certainly happen one day: a database written by a NEWER daemon, whose
    // tables this build does not understand.
    const int found = schema_version();
    if (found != kSchemaVersion) {
        throw std::runtime_error(
            "database schema is version " + std::to_string(found) + ", but this "
            "build of bandsightd expects version " + std::to_string(kSchemaVersion) +
            ". No migration exists between them. Move the file aside to start a "
            "fresh database (the history in it will be lost), or run a build that "
            "matches it.");
    }
}

int Store::schema_version() {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT value FROM config WHERE key='schema_version'", -1,
                           &st, nullptr) != SQLITE_OK) {
        return 0;
    }
    int version = 0;
    if (sqlite3_step(st) == SQLITE_ROW) version = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return version;
}

std::int64_t Store::upsert_app(const std::string& exe, const std::string& name,
                               const std::string& cgroup_hint) {
    sqlite3_stmt* st = nullptr;
    const char* sql =
        "INSERT INTO apps(exe, name, cgroup_hint) VALUES(?,?,?) "
        "ON CONFLICT(exe) DO UPDATE SET name=excluded.name, cgroup_hint=excluded.cgroup_hint "
        "RETURNING id";
    if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) {
        throw std::runtime_error("upsert_app prepare");
    }
    sqlite3_bind_text(st, 1, exe.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, cgroup_hint.c_str(), -1, SQLITE_TRANSIENT);
    std::int64_t id = 0;
    if (sqlite3_step(st) == SQLITE_ROW) id = sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    if (id == 0) throw std::runtime_error("upsert_app returned no id");
    return id;
}

std::string Store::get_config(const std::string& key, const std::string& fallback) {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT value FROM config WHERE key=?", -1, &st,
                           nullptr) != SQLITE_OK) {
        return fallback;
    }
    sqlite3_bind_text(st, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    std::string out = fallback;
    if (sqlite3_step(st) == SQLITE_ROW) {
        const unsigned char* v = sqlite3_column_text(st, 0);
        if (v) out = reinterpret_cast<const char*>(v);
    }
    sqlite3_finalize(st);
    return out;
}

void Store::set_config(const std::string& key, const std::string& value) {
    sqlite3_stmt* st = nullptr;
    const char* sql =
        "INSERT INTO config(key, value) VALUES(?,?) "
        "ON CONFLICT(key) DO UPDATE SET value=excluded.value";
    if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) {
        throw std::runtime_error("set_config prepare");
    }
    sqlite3_bind_text(st, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, value.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

void Store::upsert_iface(int ifindex, const std::string& name, const std::string& kind) {
    sqlite3_stmt* st = nullptr;
    const char* sql =
        "INSERT INTO ifaces(id, name, kind) VALUES(?,?,?) "
        "ON CONFLICT(id) DO UPDATE SET name=excluded.name, kind=excluded.kind";
    if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) {
        throw std::runtime_error("upsert_iface prepare");
    }
    sqlite3_bind_int(st, 1, ifindex);
    sqlite3_bind_text(st, 2, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, kind.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) != SQLITE_DONE) {
        sqlite3_finalize(st);
        throw std::runtime_error("upsert_iface step");
    }
    sqlite3_finalize(st);
}

void Store::write_samples(std::int64_t ts, const std::vector<IfaceDelta>& ifaces,
                          const std::vector<AppDelta>& apps) {
    exec("BEGIN");
    try {
        sqlite3_stmt* si = nullptr;
        if (sqlite3_prepare_v2(db_,
                "INSERT INTO iface_samples(res,ts,iface_id,rx,tx) VALUES(1,?,?,?,?) "
                "ON CONFLICT(res,ts,iface_id) DO UPDATE SET rx=rx+excluded.rx, tx=tx+excluded.tx",
                -1, &si, nullptr) != SQLITE_OK) {
            // sqlite3_prepare_v2 guarantees *si is null on failure, so there is
            // nothing to finalize here; a leaked statement is impossible on this path.
            throw std::runtime_error("iface_samples prepare");
        }
        for (const auto& d : ifaces) {
            sqlite3_reset(si);
            sqlite3_bind_int64(si, 1, ts);
            sqlite3_bind_int(si, 2, d.ifindex);
            sqlite3_bind_int64(si, 3, static_cast<sqlite3_int64>(d.rx));
            sqlite3_bind_int64(si, 4, static_cast<sqlite3_int64>(d.tx));
            if (sqlite3_step(si) != SQLITE_DONE) {
                sqlite3_finalize(si);
                throw std::runtime_error("iface_samples insert");
            }
        }
        sqlite3_finalize(si);

        sqlite3_stmt* sa = nullptr;
        if (sqlite3_prepare_v2(db_,
                "INSERT INTO app_samples(res,ts,app_id,iface_id,rx,tx) VALUES(1,?,?,?,?,?) "
                "ON CONFLICT(res,ts,app_id,iface_id) DO UPDATE SET rx=rx+excluded.rx, tx=tx+excluded.tx",
                -1, &sa, nullptr) != SQLITE_OK) {
            throw std::runtime_error("app_samples prepare");
        }
        for (const auto& d : apps) {
            sqlite3_reset(sa);
            sqlite3_bind_int64(sa, 1, ts);
            sqlite3_bind_int64(sa, 2, d.app_id);
            sqlite3_bind_int(sa, 3, d.ifindex);
            sqlite3_bind_int64(sa, 4, static_cast<sqlite3_int64>(d.rx));
            sqlite3_bind_int64(sa, 5, static_cast<sqlite3_int64>(d.tx));
            if (sqlite3_step(sa) != SQLITE_DONE) {
                sqlite3_finalize(sa);
                throw std::runtime_error("app_samples insert");
            }
        }
        sqlite3_finalize(sa);
    } catch (...) {
        // SQLite auto-rolls-back on SQLITE_FULL, so this ROLLBACK then fails
        // with "no transaction is active" - and THAT became the reported error,
        // masking the disk-full that actually caused it (C4 residual). The
        // original exception is what the caller needs.
        try {
            exec("ROLLBACK");
        } catch (const std::exception& rollback_failed) {
            std::fprintf(stderr, "bandsightd: rollback after a failed write also "
                                 "failed (%s); reporting the original error\n",
                         rollback_failed.what());
        }
        throw;
    }
    exec("COMMIT");
}

}  // namespace bandsight
