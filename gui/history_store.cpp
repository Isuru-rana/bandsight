// SPDX-License-Identifier: GPL-3.0-or-later
#include "history_store.h"

#include <QDateTime>
#include <QDebug>

namespace bandsight::gui {

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

// The query failed: core keeps SQLite's message, and a warning is the only
// trace a failure leaves (no signal carries it - see querySeries).
void warn(const char* what, const core::HistoryDb& db) {
    qWarning() << "bandsight:" << what << QString::fromStdString(db.last_query_error());
}

}  // namespace

void HistoryStore::open(const QString& db_path) {
    // SQLite itself enforces read-only even though the filesystem allows writes
    // (spec §3). Check::None: a file that opens but cannot be read is reported
    // by the first query, as it always has been in the GUI.
    if (!db_.open(db_path.toStdString(), core::HistoryDb::Check::None)) {
        emit openFailed(db_path, qs(db_.error()));
        return;
    }
    emit opened(db_path);
}

void HistoryStore::querySeries(Window w, QSet<int> ifindexes, qint64 now) {
    const WindowSpec spec = window_spec(w);
    const core::Span span = core::window_span(spec.res, spec.range_s, now);
    const std::vector<int> ids(ifindexes.begin(), ifindexes.end());
    // No queryFailed signal, by the brief's explicit decision. The consequence
    // is worth stating because the two queries degrade DIFFERENTLY: a failure
    // here still emits a full, correctly-sized, all-zero series - which is
    // indistinguishable from a genuinely idle network - whereas
    // queryIfaceTotals emits an EMPTY vector. So a caller cannot detect "the
    // query failed" from either payload, and must not dispatch any query
    // before opened() has arrived. This warning is the only trace it leaves.
    bool ok = true;
    const auto points = db_.series(spec.res, span.from, span.to, ids, &ok);
    if (!ok) warn("series query failed, emitting an all-zero series:", db_);

    // Zero-filled by core: one point per bucket. An idle bucket is a zero
    // observation, never a gap (spec §4.3) - and the count equals the point
    // budget exactly.
    QVector<SeriesPoint> out;
    out.reserve(qsizetype(points.size()));
    for (const auto& p : points) out.push_back({p.ts, p.rx, p.tx});
    emit series(w, out);
}

void HistoryStore::queryIfaceTotals(Window w, qint64 now) {
    const WindowSpec spec = window_spec(w);
    // Bounded above at the end of the grid, not left open (FOLLOWUP B9): an
    // unbounded query fetched every row from the window start to the end of the
    // table - about 30x overfetch on the Day window - and would also count a
    // future-dated row written across a clock jump.
    const core::Span span = core::window_span(spec.res, spec.range_s, now);
    queryIfaceTotalsRange(span.from, span.to, spec.res);
}

void HistoryStore::queryIfaceTotalsRange(qint64 from, qint64 to, int res) {
    QVector<IfaceTotal> out;
    if (const auto totals = db_.iface_totals(from, to, res)) {
        for (const core::IfaceTotal& c : *totals) {
            IfaceTotal t;
            t.id = c.id;
            t.name = qs(c.name);
            t.kind = qs(c.kind);
            t.rx = c.rx;
            t.tx = c.tx;
            for (int id : c.ids) t.ids.push_back(id);
            out.push_back(t);
        }
    } else {
        // Degrades to an EMPTY vector, unlike querySeries, which degrades to a
        // full all-zero series (see the note there). Same caller rule: no query
        // may be dispatched before opened() has arrived.
        warn("iface totals query failed, emitting an empty table:", db_);
    }
    emit ifaceTotals(out);
}

// Restricted to the interface kinds the Interfaces tab calls measured, plus row
// 0 ("unknown"): see core::HistoryDb::app_totals for why, and kind_is_measured()
// for the same rule on this side - the two must be changed together.
void HistoryStore::queryAppTotals(Window w, qint64 now) {
    const WindowSpec spec = window_spec(w);
    const core::Span span = core::window_span(spec.res, spec.range_s, now);
    queryAppTotalsRange(span.from, span.to, spec.res);
}

void HistoryStore::queryAppTotalsRange(qint64 from, qint64 to, int res) {
    QVector<AppTotal> out;
    const auto totals = db_.app_totals(from, to, res);
    if (totals) {
        for (const core::AppTotal& c : *totals)
            out.push_back({c.id, qs(c.exe), qs(c.name), c.rx, c.tx});
    } else {
        // Empty vector on failure, like queryIfaceTotals; ok=false says so.
        warn("app totals query failed, emitting an empty table:", db_);
    }
    emit appTotals(out, totals.has_value());
}

void HistoryStore::queryHostTotals(Window w, qint64 now) {
    const WindowSpec spec = window_spec(w);
    // To `now`, not the end of the grid: the daemon writes the current minute
    // live, so stopping at the last closed bucket would hide up to a minute.
    emitHostPairs(db_.host_pairs_window(spec.res, spec.range_s, now));
}

void HistoryStore::queryHostTotalsRange(qint64 from, qint64 to, int res) {
    emitHostPairs(db_.host_pairs_range(from, to, res, QDateTime::currentSecsSinceEpoch()));
}

void HistoryStore::emitHostPairs(const core::HostPairs& r) {
    if (!r.collected) {
        emit hostPairs({}, false, 0);
        return;
    }
    if (!r.ok) warn("host totals query failed, emitting an empty table:", db_);
    QVector<AppHostTotal> out;
    out.reserve(qsizetype(r.pairs.size()));
    for (const core::AppHostTotal& c : r.pairs)
        out.push_back({c.app_id, qs(c.exe), qs(c.app_name), c.host_id, qs(c.ip),
                       qs(c.hostname), c.rx, c.tx});
    // A failed query still reads as collected, with no pairs: that is what the
    // Hosts column has always been told.
    emit hostPairs(out, true, r.since);
}

void HistoryStore::queryTypeTotals(Window w, qint64 now) {
    const WindowSpec spec = window_spec(w);
    emitTypeTotals(db_.type_totals_window(spec.res, spec.range_s, now));   // see queryHostTotals
}

void HistoryStore::queryTypeTotalsRange(qint64 from, qint64 to, int res) {
    emitTypeTotals(db_.type_totals_range(from, to, res, QDateTime::currentSecsSinceEpoch()));
}

void HistoryStore::emitTypeTotals(const core::TypeTotals& r) {
    if (!r.collected) {
        emit typeTotals({}, false, 0, true);
        return;
    }
    if (!r.ok) warn("type totals query failed, emitting an empty table:", db_);
    QVector<TypeTotal> out;
    out.reserve(qsizetype(r.totals.size()));
    for (const core::TypeTotal& c : r.totals) out.push_back({c.proto, c.port, c.rx, c.tx});
    emit typeTotals(out, true, r.since, r.ok);
}

// Usage against the data cap (spec §7): see core::HistoryDb::quota for why it
// reads two tiers.
void HistoryStore::queryQuota(qint64 now) {
    const core::Quota q = db_.quota(now);
    QuotaInfo info;
    info.used = q.used;
    info.cap = q.cap;
    info.projected = q.projected;
    info.cycle_start = q.cycle_start;
    info.cycle_end = q.cycle_end;
    info.cycle_day = q.cycle_day;
    emit quota(info);
}

}  // namespace bandsight::gui
