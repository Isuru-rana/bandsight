// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QSet>
#include <QString>
#include <QVector>

#include "core/history_db.h"
#include "frame_types.h"
#include "quota.h"

namespace bandsight::gui {

// Read-only reader for the daemon's database (spec §3). Moved to a worker
// QThread by Task 10, so every slot below - including open() and the
// destructor - runs on whichever thread owns the object. That is deliberate: a
// database handle is opened, used and closed on one thread.
//
// The queries themselves are core::HistoryDb's (core/history_db.h), shared with
// the command-line client; this class is their Qt face - slots in, signals out,
// Qt types on both.
class HistoryStore : public QObject {
    Q_OBJECT
public:
    explicit HistoryStore(QObject* parent = nullptr) : QObject(parent) {}

public slots:
    void open(const QString& db_path);  // emits opened() or openFailed()
    void querySeries(Window w, QSet<int> ifindexes, qint64 now);
    void queryIfaceTotals(Window w, qint64 now);
    void queryAppTotals(Window w, qint64 now);
    void queryQuota(qint64 now);

    // Totals for an arbitrary span, for the timeline's range selection. Same
    // signals as the window-based queries, so nothing downstream learns that a
    // selection exists - a sub-range is still just "the totals on screen".
    void queryIfaceTotalsRange(qint64 from, qint64 to, int res);
    void queryAppTotalsRange(qint64 from, qint64 to, int res);

    // (app, host) pairs for the Hosts column. Host rows are minute-grained, so a
    // seconds-tier window or selection is widened to whole minutes, and the
    // current minute - written live by the daemon - is included.
    void queryHostTotals(Window w, qint64 now);
    void queryHostTotalsRange(qint64 from, qint64 to, int res);

    // Bytes per service for the Traffic type column. Minute-grained like the
    // host rows, and widened and prorated the same way.
    void queryTypeTotals(Window w, qint64 now);
    void queryTypeTotalsRange(qint64 from, qint64 to, int res);

signals:
    // Both carry the path they refer to. Without it a caller cannot tell a reply
    // to its current open from a reply to one it superseded, which is B5: two
    // opens can be outstanding when the daemon names a different database.
    void opened(const QString& db_path);
    void openFailed(const QString& db_path, const QString& reason);  // + SQLite's message
    void series(Window w, QVector<SeriesPoint> points);
    void ifaceTotals(QVector<IfaceTotal> totals);
    // ok=false: the query failed and totals is empty, which is not the same
    // as "no application moved anything" (a degraded daemon, BPF not loaded).
    void appTotals(QVector<AppTotal> totals, bool ok);
    // collected=false: the database predates host collection (no iface_id
    // column) - an old daemon, not an idle network. since: when host recording
    // began, if that is inside the period (0 otherwise) - app history is older
    // than host history, so the two cannot be compared before it.
    void hostPairs(QVector<AppHostTotal> pairs, bool collected, qint64 since);
    // collected=false: no type_samples table (only a daemon older than the
    // Traffic type column has touched the database). since: when type
    // recording began, if that is inside the period (0 otherwise). ok=false:
    // the query failed and totals is empty, which is not an idle period.
    void typeTotals(QVector<TypeTotal> totals, bool collected, qint64 since, bool ok);
    void quota(QuotaInfo info);

private:
    // The host and type answers proper, from core's result to the signal.
    void emitHostPairs(const core::HostPairs& r);
    void emitTypeTotals(const core::TypeTotals& r);

    core::HistoryDb db_;
};

}  // namespace bandsight::gui

// QSignalSpy stores signal arguments as QVariant, and queued cross-thread
// delivery (Task 10) needs the same metatypes.
Q_DECLARE_METATYPE(bandsight::gui::Window)
Q_DECLARE_METATYPE(bandsight::gui::SeriesPoint)
Q_DECLARE_METATYPE(bandsight::gui::IfaceTotal)
Q_DECLARE_METATYPE(QVector<bandsight::gui::SeriesPoint>)
Q_DECLARE_METATYPE(QVector<bandsight::gui::IfaceTotal>)
Q_DECLARE_METATYPE(bandsight::gui::QuotaInfo)
