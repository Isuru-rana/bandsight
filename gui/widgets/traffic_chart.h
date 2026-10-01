// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QVector>
#include <QWidget>

#include "frame_types.h"

namespace bandsight::gui {

// The Graph tab's chart: download and upload as smooth, overlapping areas rising
// from a common floor, over alternating time bands, with one reference line.
//
// Custom-painted rather than QtCharts. QtCharts can neither draw the time bands
// nor overlap two areas cleanly, and its spline overshoots (see smooth.h).
//
// Overlap means one series can stand in front of the other. Both fills are
// opaque - translucent blending was tried and rejected by the palette validator,
// see traffic_chart.cpp - with upload in front. Each curve is also drawn as a
// line with a 2px surface halo, and download's line goes on last, so neither
// series can disappear behind the other whichever is larger.
class TrafficChart : public QWidget {
    Q_OBJECT
public:
    explicit TrafficChart(QWidget* parent = nullptr);

    // `window` decides the units: the 5-minute window holds bytes per second, so
    // its scale is a rate; longer windows hold bytes per bucket, a volume.
    void setSeries(const QVector<SeriesPoint>& points, Window window);

    // What is being drawn, before smoothing. For tests: painting is checked
    // separately, by pixel.
    const QVector<SeriesPoint>& points() const { return points_; }
    Window window() const { return window_; }

    // The reference line's value and label, so a test can check the scale says
    // something true about the data.
    quint64 referenceValue() const;
    QString referenceLabel() const;

    // Where the data is drawn, and the value at the top edge of it. Public so a
    // pixel test can locate a value on screen from the same arithmetic the paint
    // uses, instead of hardcoding margins that drift.
    QRectF plotRect() const;
    double yMax() const;

    QSize sizeHint() const override { return {640, 320}; }
    QSize minimumSizeHint() const override { return {240, 140}; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QVector<SeriesPoint> points_;
    Window window_ = Window::FiveMin;
};

// A "nice" value at or below `v` - 1, 2 or 5 times a power of ten - for the
// reference line, so the label reads as a round number rather than as 387.21 KB.
quint64 nice_floor(quint64 v);

}  // namespace bandsight::gui
