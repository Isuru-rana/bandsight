// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QVector>
#include <QWidget>

#include "frame_types.h"

namespace bandsight::gui {

// The band across the bottom: download above the baseline, upload mirrored
// below, with time labels. Dragging across it selects a sub-range, which
// re-queries the totals above; a click clears the selection.
class TimelineStrip : public QWidget {
    Q_OBJECT
public:
    explicit TimelineStrip(QWidget* parent = nullptr);

    void setSeries(const QVector<SeriesPoint>& points);
    int pointCount() const { return int(points_.size()); }

    // The selected span, or {0,0} when the whole period is shown.
    qint64 selectionFrom() const { return sel_from_; }
    qint64 selectionTo() const { return sel_to_; }
    bool hasSelection() const { return sel_to_ > sel_from_; }
    void clearSelection();

    // Pure mapping, exposed so the arithmetic is testable without a mouse: an
    // x in widget coordinates to the timestamp of the bucket under it.
    qint64 timestampAt(int x) const;

    QSize sizeHint() const override { return {600, 92}; }

signals:
    void rangeSelected(qint64 from, qint64 to);
    void selectionCleared();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;

private:
    QVector<SeriesPoint> points_;
    qint64 sel_from_ = 0;
    qint64 sel_to_ = 0;
    int drag_start_x_ = -1;
    int drag_now_x_ = -1;
};

}  // namespace bandsight::gui
