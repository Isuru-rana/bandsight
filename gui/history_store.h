// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QSet>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

#include "frame_types.h"
#include "quota.h"

namespace bandsight::gui {

// Read-only reader for the daemon's database (spec §3). Moved to a worker
// QThread by Task 10, so every slot below - including open() and the
// destructor - runs on whichever thread owns the object. That is deliberate: a
// QSqlDatabase connection must be opened, used and closed on one thread.
class HistoryStore : public QObject {
    Q_OBJECT
public:
    explicit HistoryStore(QObject* parent = nullptr) : QObject(parent) {}
    ~HistoryStore() override;

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

signals:
    // Both carry the path they refer to. Without it a caller cannot tell a reply
    // to its current open from a reply to one it superseded, which is B5: two
    // opens can be outstanding when the daemon names a different database.
    void opened(const QString& db_path);
    void openFailed(const QString& db_path, const QString& reason);  // + SQLite's message
    void series(Window w, QVector<SeriesPoint> points);
    void ifaceTotals(QVector<IfaceTotal> totals);
    void appTotals(QVector<AppTotal> totals);
    void quota(QuotaInfo info);

private:
    // Drops our QSqlDatabase copy and unregisters the connection name, in that
    // order - removeDatabase() warns loudly if any copy is still alive.
    void closeDb();

    // The config table is read here, never written - the GUI's handle is
    // read-only and writes go through the daemon's set_config command.
    QString config_value(const QString& key, const QString& fallback);
    quint64 config_u64(const QString& key, quint64 fallback);
    int config_int(const QString& key, int fallback);

    QSqlDatabase db_;
    QString conn_name_;
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
