// SPDX-License-Identifier: GPL-3.0-or-later
#include "history_store.h"

#include <QDebug>
#include <QHash>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>

#include "time_buckets.h"

#include "quota.h"

namespace bandsight::gui {

namespace {
QString in_list(const QSet<int>& ids) {   // ints only - no injection surface
    QStringList l;
    for (int id : ids) l << QString::number(id);
    return l.join(',');
}

// The zero-fill grid must land on exactly the timestamps the daemon writes, so
// it is generated with the daemon's own bucket_start/bucket_next rather than
// re-derived arithmetically. res=1 and res=60 are plain modulo, but res=3600 is
// floored to the LOCAL hour via mktime - in any zone whose offset is not a whole
// number of hours (UTC+5:30 here) a `ts / 3600 * 3600` grid is offset from every
// stored row by the sub-hour part of the zone, and the res=3600 tier reads as
// permanently empty. Deriving the grid from the same function that produced the
// rows makes the two agree by construction, in every zone and across DST.
qint64 grid_start(qint64 now, int res, qint64 range_s) {
    return bandsight::bucket_start(now - range_s, res);
}
// One past the newest CLOSED bucket: rollup_tier only writes buckets strictly
// before bucket_start(now), so this is exactly where the data ends.
qint64 grid_end(qint64 now, int res) { return bandsight::bucket_start(now, res); }
}  // namespace

// A QSqlDatabase member is a refcounted handle, not an owner: closing it is not
// enough. The connection stays in Qt's global registry under conn_name_, so a
// later HistoryStore that lands on the same heap/stack address (which is what
// happens across test cases) builds the same name and addDatabase() prints
// "duplicate connection name ... old connection removed", after which the first
// object's still-open handle is dangling in the registry. Unregistering here
// keeps the registry exactly as empty as the object graph.
void HistoryStore::closeDb() {
    if (conn_name_.isEmpty()) return;
    if (db_.isValid() && db_.isOpen()) db_.close();
    db_ = QSqlDatabase();  // drop our copy before removing, or Qt warns
    QSqlDatabase::removeDatabase(conn_name_);
    conn_name_.clear();
}

HistoryStore::~HistoryStore() { closeDb(); }

void HistoryStore::open(const QString& db_path) {
    closeDb();  // reopening must not orphan the previous connection
    conn_name_ = QStringLiteral("history_%1").arg(quintptr(this));
    db_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn_name_);
    db_.setDatabaseName(db_path);
    // SQLite itself enforces read-only even though the filesystem allows
    // writes (spec §3).
    db_.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
    if (!db_.open()) {
        const QString reason = db_.lastError().text();
        closeDb();  // a failed open still registered the name
        emit openFailed(db_path, reason);
        return;
    }
    emit opened(db_path);
}

void HistoryStore::querySeries(Window w, QSet<int> ifindexes, qint64 now) {
    const WindowSpec spec = window_spec(w);
    const qint64 start = grid_start(now, spec.res, spec.range_s);
    const qint64 end = grid_end(now, spec.res);

    QHash<qint64, SeriesPoint> rows;
    QSqlQuery q(db_);
    q.prepare(QStringLiteral(
        "SELECT ts, SUM(rx), SUM(tx) FROM iface_samples "
        "WHERE res=? AND ts>=? AND iface_id IN (%1) GROUP BY ts").arg(in_list(ifindexes)));
    q.addBindValue(spec.res);
    q.addBindValue(start);
    // No queryFailed signal, by the brief's explicit decision. The consequence
    // is worth stating because the two queries degrade DIFFERENTLY: a failure
    // here still emits a full, correctly-sized, all-zero series - which is
    // indistinguishable from a genuinely idle network - whereas
    // queryIfaceTotals emits an EMPTY vector. So a caller cannot detect "the
    // query failed" from either payload, and must not dispatch any query
    // before opened() has arrived. This warning is the only trace it leaves.
    if (q.exec()) {
        while (q.next())
            rows.insert(q.value(0).toLongLong(),
                        {q.value(0).toLongLong(),
                         q.value(1).toULongLong(), q.value(2).toULongLong()});
    } else {
        qWarning() << "bandsight: series query failed, emitting an all-zero"
                   << "series:" << q.lastError().text();
    }

    // Zero-fill: one point per bucket. An idle bucket is a zero observation,
    // never a gap (spec §4.3) - and the count equals the point budget exactly.
    QVector<SeriesPoint> out;
    out.reserve(int(spec.range_s / spec.res));
    for (qint64 ts = start; ts < end; ts = bandsight::bucket_next(ts, spec.res))
        out.push_back(rows.value(ts, SeriesPoint{ts, 0, 0}));
    emit series(w, out);
}

void HistoryStore::queryIfaceTotals(Window w, qint64 now) {
    const WindowSpec spec = window_spec(w);
    // Bounded above at the end of the grid, not left open (FOLLOWUP B9): an
    // unbounded query fetched every row from the window start to the end of the
    // table - about 30x overfetch on the Day window - and would also count a
    // future-dated row written across a clock jump.
    queryIfaceTotalsRange(grid_start(now, spec.res, spec.range_s),
                          grid_end(now, spec.res), spec.res);
}

void HistoryStore::queryIfaceTotalsRange(qint64 from, qint64 to, int res) {
    const qint64 start = from;

    QVector<IfaceTotal> out;
    QSqlQuery q(db_);
    q.prepare(QStringLiteral(
        "SELECT i.id, i.name, i.kind, "
        "COALESCE(SUM(s.rx),0), COALESCE(SUM(s.tx),0) "
        "FROM ifaces i LEFT JOIN iface_samples s "
        "  ON s.iface_id = i.id AND s.res=? AND s.ts>=? AND s.ts<=? "
        "WHERE i.id != 0 GROUP BY i.id ORDER BY i.name"));
    q.addBindValue(res);
    q.addBindValue(start);
    q.addBindValue(to);
    if (q.exec()) {
        while (q.next()) {
            IfaceTotal t;
            t.id = q.value(0).toInt();
            t.name = q.value(1).toString();
            t.kind = q.value(2).toString();
            t.rx = q.value(3).toULongLong();
            t.tx = q.value(4).toULongLong();
            out.push_back(t);
        }
    } else {
        // Degrades to an EMPTY vector, unlike querySeries, which degrades to a
        // full all-zero series (see the note there). Same caller rule: no query
        // may be dispatched before opened() has arrived.
        qWarning() << "bandsight: iface totals query failed, emitting an empty"
                   << "table:" << q.lastError().text();
    }
    emit ifaceTotals(out);
}

// Restricted to the same interface kinds the Interfaces tab calls measured, and
// for the same reason (spec §2.2): loopback and tunnel bytes are either counted
// again on the physical interface underneath or never leave the machine. Summing
// them here made a python process that talks to itself the largest consumer on
// the system by three orders of magnitude - 4.83 TB in thirty days against 17 GB
// of real traffic - while the Interfaces tab on the next tab along correctly
// showed lo as "not measured". The kinds are spelled out in SQL rather than
// filtered in C++ so the database does the work; kind_is_measured() is the same
// rule and the two must be changed together.
//
// It also drops rows whose iface_id is 0 (unknown egress), which the JOIN
// excludes: an interface nobody could name cannot be shown to be measured.
//
// Unlike queryIfaceTotals this INNER JOINs on app_samples, and deliberately so: an interface
// with no samples is a fact worth showing ("not measured", spec §2.2), but the
// apps table accumulates every executable ever seen - 161 of them on the
// development machine - and listing the ones that moved nothing this window
// would bury the handful that did, which is exactly the failure U2 documents on
// the Interfaces tab.
void HistoryStore::queryAppTotals(Window w, qint64 now) {
    const WindowSpec spec = window_spec(w);
    queryAppTotalsRange(grid_start(now, spec.res, spec.range_s),
                        grid_end(now, spec.res), spec.res);
}

void HistoryStore::queryAppTotalsRange(qint64 from, qint64 to, int res) {
    const qint64 start = from;

    QVector<AppTotal> out;
    QSqlQuery q(db_);
    q.prepare(QStringLiteral(
        "SELECT a.id, a.exe, a.name, SUM(s.rx), SUM(s.tx) "
        "FROM apps a JOIN app_samples s ON s.app_id = a.id "
        "JOIN ifaces i ON i.id = s.iface_id "
        "WHERE s.res=? AND s.ts>=? AND s.ts<=? AND i.kind IN ('physical','wifi') "
        "GROUP BY a.id HAVING SUM(s.rx) + SUM(s.tx) > 0 "
        "ORDER BY SUM(s.rx) + SUM(s.tx) DESC"));
    q.addBindValue(res);
    q.addBindValue(start);
    q.addBindValue(to);
    if (q.exec()) {
        while (q.next()) {
            AppTotal t;
            t.id = q.value(0).toInt();
            t.exe = q.value(1).toString();
            t.name = q.value(2).toString();
            t.rx = q.value(3).toULongLong();
            t.tx = q.value(4).toULongLong();
            out.push_back(t);
        }
    } else {
        // Empty vector on failure, like queryIfaceTotals - see the note there
        // for why neither payload lets a caller detect the failure, and why no
        // query may be dispatched before opened().
        qWarning() << "bandsight: app totals query failed, emitting an empty"
                   << "table:" << q.lastError().text();
    }
    emit appTotals(out);
}

namespace {
quint64 sum_over(QSqlDatabase& db, int res, qint64 from, qint64 to,
                 quint64& tx_out) {
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT COALESCE(SUM(s.rx),0), COALESCE(SUM(s.tx),0) "
        "FROM iface_samples s JOIN ifaces i ON i.id = s.iface_id "
        "WHERE s.res=? AND s.ts>=? AND s.ts<? "
        "  AND i.kind IN ('physical','wifi')"));
    q.addBindValue(res);
    q.addBindValue(from);
    q.addBindValue(to);
    if (!q.exec() || !q.next()) return 0;
    tx_out += q.value(1).toULongLong();
    return q.value(0).toULongLong();
}
}  // namespace

// Usage against the data cap (spec §7). Two tiers, not one, and that is the
// whole difficulty: rollup_tier only writes CLOSED buckets, so the res=86400
// tier's newest row is up to two days old - 34 hours on the development machine
// when this was written. Summing dailies alone would under-report a monthly cap
// by up to 7%, always in the direction of claiming more headroom than exists,
// which is the one direction a cap indicator must never err in.
//
// So: whole closed days from res=86400, then everything after the last closed
// day from res=60, which is retained for 30 days and therefore always covers the
// tail. The boundary is taken from the data (MAX(ts) actually present) rather
// than from the clock, so a lagging or stalled rollup shifts the split instead
// of opening a hole.
void HistoryStore::queryQuota(qint64 now) {
    QuotaInfo info;
    info.cycle_day = config_int("cycle_start_day", 1);
    info.cap = config_u64("cap_bytes", 0);

    const Cycle c = cycle_for(now, info.cycle_day);
    info.cycle_start = c.start;
    info.cycle_end = c.end;

    qint64 boundary = c.start;
    {
        QSqlQuery q(db_);
        q.prepare(QStringLiteral(
            "SELECT MAX(ts) FROM iface_samples WHERE res=86400 AND ts>=? AND ts<?"));
        q.addBindValue(c.start);
        q.addBindValue(now);
        if (q.exec() && q.next() && !q.value(0).isNull())
            boundary = q.value(0).toLongLong() + 86400;
    }

    quint64 tx = 0;
    quint64 rx = sum_over(db_, 86400, c.start, boundary, tx);
    rx += sum_over(db_, 60, boundary, now + 1, tx);

    // A cap counts what crossed the link, in both directions: an ISP does not
    // bill upload separately.
    info.used = rx + tx;
    info.projected = project_to_cycle_end(info.used, c, now);
    emit quota(info);
}

QString HistoryStore::config_value(const QString& key, const QString& fallback) {
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("SELECT value FROM config WHERE key=?"));
    q.addBindValue(key);
    if (q.exec() && q.next()) return q.value(0).toString();
    return fallback;
}

quint64 HistoryStore::config_u64(const QString& key, quint64 fallback) {
    bool ok = false;
    const quint64 v = config_value(key, QString()).toULongLong(&ok);
    return ok ? v : fallback;
}

int HistoryStore::config_int(const QString& key, int fallback) {
    bool ok = false;
    const int v = config_value(key, QString()).toInt(&ok);
    return ok ? v : fallback;
}

}  // namespace bandsight::gui
