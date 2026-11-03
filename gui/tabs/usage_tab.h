// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QSet>
#include <QVector>
#include <QWidget>

#include "frame_types.h"

class QComboBox;
class QLabel;

namespace bandsight::gui {

class Card;
class DonutChart;
class RankedList;
class StatPair;
class TimelineStrip;

// Spec §7's Usage screen, in the dense four-column shape: a total split
// download/upload, then Apps, then Hosts, then Traffic type (bytes by service
// port), over a timeline of the same period.
class UsageTab : public QWidget {
    Q_OBJECT
public:
    explicit UsageTab(QWidget* parent = nullptr);

    // Test-only accessors: the wiring is the behaviour.
    DonutChart* donut() const { return donut_; }
    StatPair* stats() const { return stats_; }
    RankedList* apps() const { return apps_; }
    TimelineStrip* timeline() const { return timeline_; }
    Card* hostsCard() const { return hosts_; }
    Card* trafficCard() const { return traffic_; }
    RankedList* hosts() const { return hosts_list_; }
    QLabel* hostsNote() const { return hosts_note_; }   // objectName "hostsNote"
    RankedList* traffic() const { return traffic_list_; }
    QLabel* trafficNote() const { return traffic_note_; }   // objectName "trafficNote"
    QSet<QString> excludedHosts() const;

public slots:
    // Per-application rows for the window. complete=false: the query failed,
    // so the rows are not the window's applications and nothing is computed
    // from them (no Unattributed row).
    void setTotals(const QVector<AppTotal>& totals, bool complete = true);

    // A new headline, app list and host list have been requested together.
    // The Unattributed rows keep their current values until the headline and
    // the matching list have both arrived; they are never computed from one
    // period's headline and another period's rows.
    void beginRefresh();
    // The window's overall split, which comes from the interface totals rather
    // than from summing the apps: unattributed bytes are real traffic and must
    // not vanish from the headline just because no process owned them.
    void setSplit(quint64 down, quint64 up);

    // Applications the user has clicked out of the total, by executable. The
    // donut and the ↓/↑ pair subtract them; the timeline and the data cap do
    // not - see recomputeSplit() for why.
    void toggleExcluded(const QString& exe);
    // Clears application AND host exclusions.
    void clearExcluded();

    // (app, host) pairs for the period. Summed into one row per host; kept as
    // pairs so recomputeSplit() can subtract an app and a host exclusion as a
    // union. collected=false means the database predates host collection.
    // since: when host recording began, if inside the period (0 otherwise).
    void setHostPairs(const QVector<AppHostTotal>& pairs, bool collected, qint64 since = 0);
    // Hosts the user has clicked out of the total, by host_key().
    void toggleHostExcluded(const QString& key);

    // Bytes per (protocol, service port) for the period, one row per service
    // name. Not clickable: there is no type exclusion. collected=false means
    // the daemon predates type recording; since: when it began, if inside the
    // period (0 otherwise). ok=false: the query failed, so the empty list is
    // not "no traffic".
    void setTypeTotals(const QVector<TypeTotal>& totals, bool collected, qint64 since = 0,
                       bool ok = true);
    QSet<QString> excluded() const;

    // Test-only, same rationale as the rest here.
    QLabel* excludingLabel() const { return excluding_; }
    void setSeries(const QVector<SeriesPoint>& points);

    // Totals mean nothing without the period they cover - the same coupling U1
    // documents for the Interfaces tab, and this screen is entirely totals.
    void setPeriod(const QString& label);
    // A chosen span rather than a named window: "the last 30 days" reads wrongly
    // for one, so it is a separate call rather than a string the setter sniffs.
    void setPeriodRange(qint64 from, qint64 to);
    QString period() const;

    // Reflects the application-wide window without re-emitting: MainWindow owns
    // the single source of truth (the graph's combo) and pushes it back here.
    void setWindow(Window w);
    Window window() const;
    QComboBox* windowCombo() const { return window_; }

signals:
    void windowChangeRequested(Window w);

public slots:

private:
    void recomputeSplit();

    // The unfiltered split as last reported, so toggling can subtract from it
    // repeatedly without accumulating error.
    quint64 split_down_ = 0;
    quint64 split_up_ = 0;
    QVector<AppTotal> totals_;
    QVector<AppHostTotal> pairs_;
    bool hosts_collected_ = true;
    qint64 hosts_since_ = 0;
    bool apps_complete_ = true;
    // Replies still owed: +1 per beginRefresh(), -1 as each arrives. Counts,
    // not flags, because a refresh can be issued while the previous one is
    // still on the worker; every query emits exactly once, failures included.
    int awaiting_split_ = 0;
    int awaiting_apps_ = 0;
    int awaiting_hosts_ = 0;
    void updateHostsNote();
    // The "Unattributed" footer of both lists: what the headline counted and the
    // rows do not account for. Recomputed whenever either side changes.
    void updateUnattributed();

    DonutChart* donut_;
    StatPair* stats_;
    RankedList* apps_;
    TimelineStrip* timeline_;
    Card* hosts_;
    Card* traffic_;
    RankedList* hosts_list_;
    QLabel* hosts_note_;
    RankedList* traffic_list_;
    QLabel* traffic_note_;
    QLabel* period_;
    QLabel* excluding_;
    QComboBox* window_;
};

}  // namespace bandsight::gui
