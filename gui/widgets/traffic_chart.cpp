// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgets/traffic_chart.h"

#include <QDateTime>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>

#include "smooth.h"
#include "theme.h"
#include "units.h"

namespace bandsight::gui {

namespace {
constexpr int kLabelH = 22;      // time labels under the plot
constexpr int kTopPad = 26;      // room for the legend and the reference label
constexpr double kHeadroom = 1.15;

// Both fills are OPAQUE. Translucency was the first design - it is how the app
// this is modelled on draws its overlap - and the palette validator rejected it
// at every opacity tried (35-85%). Download blue and upload orange are close to
// complementary, so blending them heads straight for grey: at 73% the overlap
// came out #b66c60, failing the chroma floor and sitting ΔE 10.2 from pure upload
// for full-colour vision, where 15 is the floor. Warm-on-warm pairs survive
// blending; this one cannot. So only the two validated colours are ever drawn,
// and the overlap is shown by layering and edges instead of by mixing.
constexpr double kEdge = 2.0;      // the data lines
constexpr double kGap = 2.0;       // surface gap either side of a line
}  // namespace

quint64 nice_floor(quint64 v) {
    if (v == 0) return 0;
    quint64 p = 1;
    while (p <= v / 10) p *= 10;
    for (quint64 m : {5ULL, 2ULL, 1ULL})
        if (m * p <= v) return m * p;
    return p;
}

TrafficChart::TrafficChart(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void TrafficChart::setSeries(const QVector<SeriesPoint>& points, Window window) {
    points_ = points;
    window_ = window;
    update();
}

QRectF TrafficChart::plotRect() const {
    return QRectF(0, kTopPad, width(), height() - kTopPad - kLabelH);
}

double TrafficChart::yMax() const {
    quint64 peak = 1;
    for (const SeriesPoint& s : points_) peak = std::max({peak, s.rx, s.tx});
    return double(peak) * kHeadroom;
}

quint64 TrafficChart::referenceValue() const {
    quint64 peak = 0;
    for (const SeriesPoint& p : points_) peak = std::max({peak, p.rx, p.tx});
    return nice_floor(peak);
}

QString TrafficChart::referenceLabel() const {
    const quint64 v = referenceValue();
    // Per-second windows hold bytes per second, so the scale is a rate; coarser
    // windows hold bytes per bucket, a volume.
    return window_spec(window_).res == 1 ? format_rate(v) : format_volume(v);
}

void TrafficChart::paintEvent(QPaintEvent*) {
    const Theme t = theme();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), t.card);

    const QRectF plot = plotRect();
    p.fillRect(plot, t.plot_bg);

    // Time bands: alternate intervals a step darker, as GlassWire does. They give
    // the eye a fixed grid to read positions against without a single grid line.
    const int bands = std::clamp(width() / 140, 2, 8);
    const double band_w = plot.width() / bands;
    for (int i = 1; i < bands; i += 2)
        p.fillRect(QRectF(plot.left() + i * band_w, plot.top(), band_w, plot.height()),
                   t.plot_band);

    // Legend, top right. Two series always get one: identity must never rest on
    // colour alone.
    {
        const QFontMetrics fm = fontMetrics();
        QString up = tr("Upload"), down = tr("Download");
        const int sw = 10, gap = 6, between = 16;
        int x = width() - 8 - (sw + gap + fm.horizontalAdvance(up)) -
                between - (sw + gap + fm.horizontalAdvance(down));
        const int y = (kTopPad - sw) / 2;
        for (auto [label, c] : {std::pair{down, t.download}, std::pair{up, t.upload}}) {
            p.setPen(Qt::NoPen);
            p.setBrush(c);
            p.drawRoundedRect(QRect(x, y, sw, sw), 2, 2);
            x += sw + gap;
            p.setPen(t.ink_muted);
            p.drawText(QRect(x, 0, fm.horizontalAdvance(label) + 2, kTopPad),
                       Qt::AlignVCenter | Qt::AlignLeft, label);
            x += fm.horizontalAdvance(label) + between;
        }
    }

    if (points_.size() < 2) {
        p.setPen(t.ink_muted);
        p.drawText(plot, Qt::AlignCenter, tr("Waiting for traffic…"));
        return;
    }

    quint64 peak = 1;
    for (const SeriesPoint& s : points_) peak = std::max({peak, s.rx, s.tx});
    const double y_max = yMax();

    // The axis spans the WINDOW, not the data. Spanning first-to-last point
    // stretched the few seconds collected after the GUI opened across the full
    // width and squeezed them as more arrived - a five-minute view whose scale
    // changed every second. Pinned to the window's length and ending at the
    // newest point, the data enters from the right and scrolls, and any part of
    // the window with no data yet is honestly empty.
    const qint64 range = window_spec(window_).range_s;
    const qint64 t1 = points_.back().ts;
    const qint64 t0 = t1 - range;
    auto x_of = [&](double ts) {
        return plot.left() + (ts - double(t0)) / double(t1 - t0) * plot.width();
    };
    auto y_of = [&](double v) { return plot.bottom() - v / y_max * plot.height(); };

    // Smoothed in DATA space, then mapped: the mapping is linear, so the monotone
    // guarantee - no overshoot below zero or above a peak - survives into pixels.
    // Dense series get fewer inserted points; past one point per few pixels the
    // curve is already smooth and extra vertices only cost paint time.
    // Spacing from the window's own point budget, not from how many points have
    // arrived: a half-filled window must not be smoothed as if it were dense.
    const double px_per_point =
        plot.width() / std::max(1.0, double(range) / double(window_spec(window_).res));
    const int per_segment = std::clamp(int(px_per_point / 3.0), 0, 8);

    auto curve = [&](bool rx) {
        QVector<QPointF> raw;
        raw.reserve(points_.size());
        for (const SeriesPoint& s : points_)
            raw.append(QPointF(double(s.ts), double(rx ? s.rx : s.tx)));
        return monotone_smooth(raw, per_segment);
    };
    auto area = [&](const QVector<QPointF>& c) {
        QPainterPath path;
        path.moveTo(x_of(c.front().x()), plot.bottom());
        for (const QPointF& q : c) path.lineTo(x_of(q.x()), y_of(q.y()));
        path.lineTo(x_of(c.back().x()), plot.bottom());
        path.closeSubpath();
        return path;
    };
    auto edge = [&](const QVector<QPointF>& c) {
        QPainterPath path;
        path.moveTo(x_of(c.front().x()), y_of(c.front().y()));
        for (const QPointF& q : c) path.lineTo(x_of(q.x()), y_of(q.y()));
        return path;
    };

    const QVector<QPointF> down = curve(true);
    const QVector<QPointF> up = curve(false);

    // A line with a surface-coloured halo: the halo is the 2px gap that makes two
    // touching fills read as two layers rather than one shape that changes colour.
    auto haloed = [&](const QPainterPath& path, const QColor& c) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(t.plot_bg, kEdge + 2 * kGap, Qt::SolidLine, Qt::RoundCap,
                      Qt::RoundJoin));
        p.drawPath(path);
        p.setPen(QPen(c, kEdge, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(path);
    };

    p.save();
    p.setClipRect(plot);
    p.fillPath(area(down), t.download);   // behind
    p.fillPath(area(up), t.upload);       // in front
    haloed(edge(up), t.upload);
    // Download's curve goes on LAST, as a line. Wherever upload is larger its
    // fill covers download's completely, and without this the download series
    // would simply vanish there - the occlusion overlap always risks.
    haloed(edge(down), t.download);
    p.restore();

    // One reference line at a round value, labelled - the whole y axis. A grid of
    // them would be ink with no information.
    const quint64 ref = nice_floor(peak);
    if (ref > 0) {
        const double ry = y_of(double(ref));
        QPen dash(t.ink_muted, 1, Qt::DashLine);
        dash.setDashPattern({3, 4});
        p.setPen(dash);
        p.drawLine(QPointF(plot.left() + 70, ry), QPointF(plot.right() - 8, ry));
        p.setPen(t.ink_muted);
        p.drawText(QRectF(plot.left() + 8, ry - 9, 62, 18), Qt::AlignVCenter | Qt::AlignLeft,
                   referenceLabel());
    }

    // Time labels at the band edges.
    p.setPen(t.ink_muted);
    const QString fmt = range <= 1800    ? QStringLiteral("HH:mm:ss")
                        : range <= 86400 ? QStringLiteral("HH:mm")
                                         : QStringLiteral("d MMM");
    for (int i = 0; i <= bands; ++i) {
        const double x = plot.left() + i * band_w;
        const qint64 ts = t0 + qint64(double(t1 - t0) * i / bands);
        const QString label = QDateTime::fromSecsSinceEpoch(ts).toString(fmt);
        const int w = fontMetrics().horizontalAdvance(label) + 8;
        const double lx = std::clamp(x - w / 2.0, 0.0, double(width() - w));
        p.drawText(QRectF(lx, plot.bottom() + 2, w, kLabelH - 2), Qt::AlignCenter, label);
    }
}

}  // namespace bandsight::gui
