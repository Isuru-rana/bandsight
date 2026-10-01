// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgets/timeline_strip.h"

#include <QDateTime>
#include <QPainter>
#include <QMouseEvent>
#include <QPainterPath>

#include "theme.h"

namespace bandsight::gui {

TimelineStrip::TimelineStrip(QWidget* parent) : QWidget(parent) {}

void TimelineStrip::setSeries(const QVector<SeriesPoint>& points) {
    points_ = points;

    // A selection is a span of TIME, not a property of the series object it was
    // drawn on. This method runs on every refresh - once a SECOND on the
    // 5-minute window, which is the live ring - so clearing here made a
    // selection last under a second in the running app while every unit test
    // passed, because no test delivered a second series.
    //
    // It survives a refresh and is dropped only when it has scrolled entirely
    // out of the data, at which point it describes nothing and would otherwise
    // linger as an invisible filter on the totals above.
    if (hasSelection() && !points_.isEmpty() &&
        (sel_to_ < points_.front().ts || sel_from_ > points_.back().ts))
        clearSelection();

    update();
}

void TimelineStrip::clearSelection() {
    const bool had = hasSelection();
    sel_from_ = sel_to_ = 0;
    drag_start_x_ = drag_now_x_ = -1;
    update();
    if (had) emit selectionCleared();
}

qint64 TimelineStrip::timestampAt(int x) const {
    if (points_.isEmpty() || width() <= 0) return 0;
    const int i = qBound(0, int(double(x) / double(width()) * points_.size()),
                         int(points_.size()) - 1);
    return points_[i].ts;
}

void TimelineStrip::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    drag_start_x_ = drag_now_x_ = int(e->position().x());
}

void TimelineStrip::mouseMoveEvent(QMouseEvent* e) {
    if (drag_start_x_ < 0) return;
    drag_now_x_ = int(e->position().x());
    update();
}

void TimelineStrip::mouseReleaseEvent(QMouseEvent* e) {
    if (drag_start_x_ < 0 || e->button() != Qt::LeftButton) return;
    const int a = qMin(drag_start_x_, drag_now_x_);
    const int b = qMax(drag_start_x_, drag_now_x_);
    drag_start_x_ = drag_now_x_ = -1;

    // A drag shorter than this is a click, and a click clears rather than
    // selecting a single bucket nobody aimed at.
    if (b - a < 6) { clearSelection(); return; }

    sel_from_ = timestampAt(a);
    sel_to_ = timestampAt(b);
    if (sel_to_ <= sel_from_) { clearSelection(); return; }
    update();
    emit rangeSelected(sel_from_, sel_to_);
}

void TimelineStrip::paintEvent(QPaintEvent*) {
    const Theme t = theme();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int label_h = fontMetrics().height() + 4;
    const QRect plot(0, 0, width(), height() - label_h);
    const double mid = plot.height() / 2.0;

    p.setPen(QPen(t.border, 1));
    p.drawLine(0, int(mid), width(), int(mid));   // recessive baseline

    if (points_.isEmpty()) {
        p.setPen(t.ink_muted);
        p.drawText(plot, Qt::AlignCenter, tr("No history for this period"));
        return;
    }

    quint64 peak = 1;
    for (const SeriesPoint& s : points_) peak = qMax(peak, qMax(s.rx, s.tx));

    // Both halves share ONE scale off the combined peak. Scaling each half to
    // its own maximum would make a trickle of upload look like a flood beside a
    // real download - two y-scales in one plot, wearing a disguise.
    const double px = double(width()) / double(points_.size());
    QPainterPath down, up;
    down.moveTo(0, mid);
    up.moveTo(0, mid);
    // A month of hourly buckets against a single large peak rounds every
    // ordinary hour to zero pixels, and the strip reads as "no data" over a
    // period that was busy all month. The scale stays linear - the shape must
    // stay truthful - but any NON-ZERO bucket is floored to one pixel, so idle
    // and active remain distinguishable at the bottom of the range.
    auto height_for = [peak, mid](quint64 v) {
        if (v == 0) return 0.0;
        return qMax(1.0, (double(v) / double(peak)) * (mid - 2));
    };
    for (int i = 0; i < points_.size(); ++i) {
        const double x = i * px;
        down.lineTo(x, mid - height_for(points_[i].rx));
        up.lineTo(x, mid + height_for(points_[i].tx));
    }
    down.lineTo(width(), mid);
    up.lineTo(width(), mid);
    down.closeSubpath();
    up.closeSubpath();

    QColor dfill = t.download; dfill.setAlpha(200);
    QColor ufill = t.upload;   ufill.setAlpha(200);
    p.setPen(Qt::NoPen);
    p.fillPath(down, dfill);
    p.fillPath(up, ufill);

    // Everything outside the selection is dimmed rather than hidden: the shape
    // of the whole period is the context that makes a selection mean anything.
    if (hasSelection() && points_.size() > 1) {
        const double span = double(points_.back().ts - points_.front().ts);
        auto x_of = [&](qint64 ts) {
            if (span <= 0) return 0.0;
            return double(ts - points_.front().ts) / span * width();
        };
        QColor veil = t.surface;
        veil.setAlpha(170);
        p.fillRect(QRectF(0, 0, x_of(sel_from_), plot.height()), veil);
        p.fillRect(QRectF(x_of(sel_to_), 0, width() - x_of(sel_to_), plot.height()), veil);
        p.setPen(QPen(t.ink_muted, 1));
        p.drawLine(QPointF(x_of(sel_from_), 0), QPointF(x_of(sel_from_), plot.height()));
        p.drawLine(QPointF(x_of(sel_to_), 0), QPointF(x_of(sel_to_), plot.height()));
    } else if (drag_start_x_ >= 0 && drag_now_x_ >= 0) {
        QColor live = t.ink_muted;
        live.setAlpha(60);
        p.fillRect(QRect(qMin(drag_start_x_, drag_now_x_), 0,
                         qAbs(drag_now_x_ - drag_start_x_), plot.height()), live);
    }

    // A handful of labels, not one per point.
    p.setPen(t.ink_muted);
    const int ticks = qMax(2, qMin(6, width() / 110));
    for (int i = 0; i <= ticks; ++i) {
        const int idx = qMin(int(points_.size()) - 1, i * (int(points_.size()) - 1) / ticks);
        const QDateTime dt = QDateTime::fromSecsSinceEpoch(points_[idx].ts);
        const QString label = dt.toString(points_.size() > 400 ? QStringLiteral("d MMM")
                                                              : QStringLiteral("HH:mm"));
        const int x = int(idx * px);
        const int w = fontMetrics().horizontalAdvance(label) + 8;
        p.drawText(QRect(qBound(0, x - w / 2, width() - w), plot.bottom() + 2, w, label_h),
                   Qt::AlignCenter, label);
    }
}

}  // namespace bandsight::gui
