// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QSet>
#include <QVector>
#include <QWidget>

#include "frame_types.h"
#include "live_ring.h"

class QComboBox;

namespace bandsight::gui {

class TotalsCard;
class TrafficChart;

// Throughput over time: download and upload as smooth, overlapping areas
// (TrafficChart). Live windows (is_live) are drawn from the live ring, seeded
// once from history when entered so they are full immediately; every longer
// window is a history query this tab asks for.
//
// Deliberately thin: it owns no logic beyond "which source, and is this reply
// still wanted". Everything summable lives in LiveRing, and everything drawn
// lives in TrafficChart.
//
// Spec §4.5 originally mirrored upload below the axis. It is now drawn
// overlapping, by request and to match the look the app is modelled on - see the
// note in the spec, and TrafficChart for how overlap is kept from hiding upload.
class GraphTab : public QWidget {
    Q_OBJECT
public:
    explicit GraphTab(QWidget* parent = nullptr);

    Window window() const;

    // The window is application-wide, not per-tab: the Usage screen sets it too,
    // and a period that differed between tabs would make "everything below
    // covers the last 30 days" false the moment you switched.
    void setWindow(Window w);

    // What the chart is actually plotting, for tests. Painting itself is checked
    // by pixel in the chart's own tests; these exist so the data-source wiring is
    // tested too.
    TrafficChart* chart() const { return chart_; }
    TotalsCard* totals() const { return totals_; }
    const QVector<SeriesPoint>& plottedPoints() const;

    // True while a live window is waiting for its history seed. MainWindow reads
    // it on its periodic refresh, because the first request is usually made
    // before the database is open and is dropped - the seed is retried until it
    // lands, then never again for that entry into the window.
    bool needsSeed() const { return needs_seed_; }

public slots:
    void onSample(const SampleFrame& f);
    void onSeries(Window w, QVector<SeriesPoint> points);
    void onVisibleChanged(QSet<int> visible);
    void onDisconnected();
    // Per-interface totals for the window, for the card's interface bars.
    void setIfaceTotals(QVector<IfaceTotal> totals);

signals:
    void seriesNeeded(Window w, QSet<int> ifindexes, qint64 now);
    void windowChanged(Window w);

    // Exactly what was plotted, whatever the source. The Usage timeline mirrors
    // this rather than re-deriving the series, which is what makes the 5-minute
    // window work there at all: that window is the live ring, never a query, so
    // a screen waiting for a history reply would sit empty forever.
    void plotted(QVector<SeriesPoint> points);

private:
    void refresh();                                // ring redraw, or ask for history
    void redraw(const QVector<SeriesPoint>& pts);

    LiveRing ring_;
    // refresh() runs on a visibility toggle as well as a window change, so the
    // signal is gated on the value actually moving. Without this every toggle
    // announced a window change, and MainWindow answered each one with a fresh
    // round of queries whose replies the supersession guard then dropped.
    Window last_window_;
    QSet<int> visible_;
    bool needs_seed_ = false;

    QVector<IfaceTotal> iface_totals_;

    QComboBox* combo_;
    TrafficChart* chart_;
    TotalsCard* totals_;
};

}  // namespace bandsight::gui
