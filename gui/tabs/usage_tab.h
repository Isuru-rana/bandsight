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
// download/upload, then Apps, then two columns held for data the daemon does not
// collect yet, over a timeline of the same period.
//
// The reserved columns are not a placeholder in the throwaway sense. They hold
// their real width and chrome so the layout the screen will have is the layout
// it has now, and so the gap is visible rather than quietly designed around.
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

public slots:
    // Per-application rows for the window.
    void setTotals(const QVector<AppTotal>& totals);
    // The window's overall split, which comes from the interface totals rather
    // than from summing the apps: unattributed bytes are real traffic and must
    // not vanish from the headline just because no process owned them.
    void setSplit(quint64 down, quint64 up);

    // Applications the user has clicked out of the total, by executable. The
    // donut and the ↓/↑ pair subtract them; the timeline and the data cap do
    // not - see recomputeSplit() for why.
    void toggleExcluded(const QString& exe);
    void clearExcluded();
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

    DonutChart* donut_;
    StatPair* stats_;
    RankedList* apps_;
    TimelineStrip* timeline_;
    Card* hosts_;
    Card* traffic_;
    QLabel* period_;
    QLabel* excluding_;
    QComboBox* window_;
};

}  // namespace bandsight::gui
