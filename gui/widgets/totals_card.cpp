// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgets/totals_card.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>

#include "theme.h"
#include "units.h"

namespace bandsight::gui {

namespace {
constexpr int kPad = 12;
constexpr int kMaxRows = 4;   // a fifth interface onward folds into "Other"
constexpr int kBarH = 6;

// A small triangle in the series colour: the colour carries identity on a mark,
// never on the text beside it.
void arrow(QPainter& p, QPointF c, bool down, const QColor& col) {
    const double s = 5;
    QPainterPath path;
    if (down) {
        path.moveTo(c.x() - s, c.y() - s / 2);
        path.lineTo(c.x() + s, c.y() - s / 2);
        path.lineTo(c.x(), c.y() + s / 2 + 2);
    } else {
        path.moveTo(c.x() - s, c.y() + s / 2);
        path.lineTo(c.x() + s, c.y() + s / 2);
        path.lineTo(c.x(), c.y() - s / 2 - 2);
    }
    path.closeSubpath();
    p.setPen(Qt::NoPen);
    p.setBrush(col);
    p.drawPath(path);
}
}  // namespace

TotalsCard::TotalsCard(QWidget* parent) : QWidget(parent) {}

void TotalsCard::setTotals(quint64 down, quint64 up) {
    down_ = down;
    up_ = up;
    update();
}

void TotalsCard::setRates(quint64 down_bps, quint64 up_bps) {
    down_rate_ = down_bps;
    up_rate_ = up_bps;
    update();
}

void TotalsCard::setIfaces(const QVector<IfaceTotal>& totals, const QSet<int>& visible) {
    QVector<IfaceTotal> rows;
    for (const IfaceTotal& t : totals)
        if (kind_is_measured(t.kind) && visible.contains(t.id)) rows.append(t);
    std::stable_sort(rows.begin(), rows.end(), [](const IfaceTotal& a, const IfaceTotal& b) {
        return a.rx + a.tx > b.rx + b.tx;
    });
    if (rows.size() > kMaxRows) {
        // Folded, never dropped: the bars must still account for every byte.
        IfaceTotal other;
        other.id = -1;
        other.name = tr("Other");
        for (int i = kMaxRows - 1; i < rows.size(); ++i) {
            other.rx += rows[i].rx;
            other.tx += rows[i].tx;
        }
        rows.resize(kMaxRows - 1);
        rows.append(other);
    }
    rows_ = rows;
    update();
}

double TotalsCard::downShare() const {
    const quint64 total = down_ + up_;
    return total ? double(down_) / double(total) : 0.0;
}

QString TotalsCard::gaugeText() const { return format_volume(down_ + up_); }

TotalsCard::Geometry TotalsCard::geometry() const {
    Geometry g;
    const QRect inner = rect().adjusted(kPad, kPad, -kPad, -kPad);
    // Interfaces take the right third when there are any; otherwise the totals
    // have the whole width rather than leaving an empty panel.
    const bool has_rows = !rows_.isEmpty();
    const int split_w = has_rows ? int(inner.width() * 0.62) : inner.width();
    const int col = split_w / 3;
    g.down = QRect(inner.left(), inner.top(), col, inner.height());
    g.gauge = QRect(inner.left() + col, inner.top(), col, inner.height());
    g.up = QRect(inner.left() + 2 * col, inner.top(), split_w - 2 * col, inner.height());
    if (!has_rows) return g;

    g.divider_x = inner.left() + split_w + kPad / 2;
    const int left = g.divider_x + kPad;
    // The rows are a block centred in the card's height, like the totals beside
    // them - not packed against the top edge.
    const int row_h = fontMetrics().height() + 8;
    const int block_h = row_h * int(rows_.size());
    g.list = QRect(left, inner.top() + (inner.height() - block_h) / 2,
                   inner.right() - left, block_h);
    for (int i = 0; i < rows_.size(); ++i)
        g.rows.append(QRect(g.list.left(), g.list.top() + i * row_h, g.list.width(), row_h));
    return g;
}

QSize TotalsCard::sizeHint() const {
    return {640, fontMetrics().height() * 5 + kPad * 2};
}

QSize TotalsCard::minimumSizeHint() const {
    return {320, fontMetrics().height() * 5 + kPad * 2};
}

void TotalsCard::paintEvent(QPaintEvent*) {
    const Theme t = theme();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(t.border);
    p.setBrush(t.card);
    p.drawRoundedRect(frame, Theme::radius_card, Theme::radius_card);

    const Geometry geo = geometry();
    const int fh = fontMetrics().height();
    QFont bold = font();
    bold.setBold(true);
    QFont big = bold;
    big.setPointSizeF(font().pointSizeF() * 1.4);

    // ---- download / upload columns --------------------------------------
    // Each column's text is centred on the column, so the two sit symmetrically
    // either side of the gauge instead of one hugging the card's edge.
    struct Side { bool down; quint64 total; quint64 rate; QColor c; QString label; QRect r; };
    const Side sides[2] = {{true, down_, down_rate_, t.download, tr("Download"), geo.down},
                           {false, up_, up_rate_, t.upload, tr("Upload"), geo.up}};
    for (const Side& s : sides) {
        const QRect& r = s.r;
        const int top = r.top() + (r.height() - int(fh * 3.7)) / 2;
        // Arrow + label centred as one unit.
        const int label_w = fontMetrics().horizontalAdvance(s.label) + 16;
        const int lx = r.center().x() - label_w / 2;
        arrow(p, QPointF(lx + 6, top + fh / 2.0), s.down, s.c);
        p.setFont(font());
        p.setPen(t.ink_muted);
        p.drawText(QRect(lx + 16, top, label_w, fh), Qt::AlignLeft | Qt::AlignVCenter, s.label);
        p.setFont(big);
        p.setPen(t.ink);
        p.drawText(QRect(r.left(), top + fh, r.width(), int(fh * 1.6)),
                   Qt::AlignHCenter | Qt::AlignVCenter, format_volume(s.total));
        p.setFont(font());
        p.setPen(t.ink_muted);
        p.drawText(QRect(r.left(), top + int(fh * 2.7), r.width(), fh),
                   Qt::AlignHCenter | Qt::AlignVCenter, tr("now %1").arg(format_rate(s.rate)));
    }

    // ---- gauge -------------------------------------------------------------
    {
        const QRect& g = geo.gauge;
        const double thickness = 10;
        const double radius = qMax(10.0, qMin(g.width() / 2.0 - 8, g.height() - fh - 8.0));
        // Arc plus the "Total" line under it, centred as one block.
        const double block = radius + fh + 2;
        const QPointF centre(g.center().x(), g.top() + (g.height() - block) / 2.0 + radius);
        const QRectF arc(centre.x() - radius + thickness / 2, centre.y() - radius + thickness / 2,
                         2 * radius - thickness, 2 * radius - thickness);
        const int half = 180 * 16;
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(t.track, thickness, Qt::SolidLine, Qt::FlatCap));
        p.drawArc(arc, half, -half);
        if (down_ + up_ > 0) {
            // Download from the left, upload from the right, a surface gap
            // between them - the same split as the Usage donut, unrolled.
            const int gap = 3 * 16;
            const int down_span = qBound(0, int(half * downShare()), half);
            const int up_span = half - down_span;
            p.setPen(QPen(t.download, thickness, Qt::SolidLine, Qt::FlatCap));
            if (down_span > gap) p.drawArc(arc, half, -(down_span - (up_span > 0 ? gap / 2 : 0)));
            p.setPen(QPen(t.upload, thickness, Qt::SolidLine, Qt::FlatCap));
            if (up_span > gap)
                p.drawArc(arc, half - down_span - (down_span > 0 ? gap / 2 : 0),
                          -(up_span - (down_span > 0 ? gap / 2 : 0)));
        }
        // Sized between the base text and the per-direction totals beside it.
        QFont headline = bold;
        headline.setPointSizeF(font().pointSizeF() * 1.2);
        const int hh = QFontMetrics(headline).height();
        p.setFont(headline);
        p.setPen(t.ink);
        p.drawText(QRectF(g.left(), centre.y() - hh - fh * 0.2, g.width(), hh),
                   Qt::AlignHCenter | Qt::AlignBottom, gaugeText());
        p.setFont(font());
        p.setPen(t.ink_muted);
        p.drawText(QRectF(g.left(), centre.y() + 2, g.width(), fh),
                   Qt::AlignHCenter | Qt::AlignTop, tr("Total"));
    }

    if (rows_.isEmpty()) return;

    // ---- interfaces ----------------------------------------------------------
    const QRect inner = rect().adjusted(kPad, kPad, -kPad, -kPad);
    p.setPen(t.border);
    p.drawLine(geo.divider_x, inner.top() + 4, geo.divider_x, inner.bottom() - 4);

    quint64 max_total = 0;
    for (const IfaceTotal& r : rows_) max_total = qMax(max_total, r.rx + r.tx);

    const int name_w = qMin(90, geo.list.width() / 3);
    const int value_w = fontMetrics().horizontalAdvance(QStringLiteral("888.8 MB")) + 6;
    const int bar_w = qMax(20, geo.list.width() - name_w - value_w - 8);
    for (int i = 0; i < rows_.size(); ++i) {
        const IfaceTotal& r = rows_[i];
        const QRect row = geo.rows[i];
        p.setPen(t.ink);
        p.drawText(QRect(row.left(), row.top(), name_w, row.height()),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   fontMetrics().elidedText(r.name, Qt::ElideRight, name_w - 4));

        // One track per row; the filled length is this interface against the
        // busiest one, split download then upload with a surface gap.
        const QRectF track(row.left() + name_w, row.center().y() - kBarH / 2.0 + 0.5, bar_w, kBarH);
        p.setPen(Qt::NoPen);
        p.setBrush(t.track);
        p.drawRoundedRect(track, Theme::radius_bar, Theme::radius_bar);
        const quint64 total = r.rx + r.tx;
        if (total > 0 && max_total > 0) {
            const double len = bar_w * (double(total) / double(max_total));
            const double dl = len * (double(r.rx) / double(total));
            const double ul = len - dl;
            const bool both = dl >= 1 && ul >= 1;
            const double g = both ? Theme::gap : 0;
            p.setBrush(t.download);
            if (dl >= 1)
                p.drawRoundedRect(QRectF(track.left(), track.top(), dl - g / 2, kBarH),
                                  Theme::radius_bar, Theme::radius_bar);
            p.setBrush(t.upload);
            if (ul >= 1)
                p.drawRoundedRect(QRectF(track.left() + dl + g / 2, track.top(), ul - g / 2, kBarH),
                                  Theme::radius_bar, Theme::radius_bar);
        }
        p.setPen(t.ink_muted);
        p.drawText(QRect(row.right() - value_w, row.top(), value_w, row.height()),
                   Qt::AlignRight | Qt::AlignVCenter, format_volume(total));
    }
}

}  // namespace bandsight::gui
