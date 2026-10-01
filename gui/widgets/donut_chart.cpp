// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgets/donut_chart.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>

#include "theme.h"
#include "units.h"

namespace bandsight::gui {

DonutChart::DonutChart(QWidget* parent) : QWidget(parent) {
    setMinimumSize(140, 140);
}

void DonutChart::setValues(quint64 down, quint64 up) {
    down_ = down;
    up_ = up;
    update();
}

QString DonutChart::centerText() const { return format_volume(down_ + up_); }

void DonutChart::paintEvent(QPaintEvent*) {
    const Theme t = theme();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int side = qMin(width(), height());
    const QRectF box((width() - side) / 2.0 + 6, (height() - side) / 2.0 + 6,
                     side - 12, side - 12);
    const double thickness = side * 0.14;   // thin mark: a ring, not a filled pie

    QPen pen;
    pen.setWidthF(thickness);
    pen.setCapStyle(Qt::FlatCap);

    // The empty state is a track ring, not a blank square: "nothing yet" and
    // "no widget here" must not look the same.
    pen.setColor(t.track);
    p.setPen(pen);
    p.drawEllipse(box.adjusted(thickness / 2, thickness / 2, -thickness / 2, -thickness / 2));

    const quint64 total = down_ + up_;
    if (total > 0) {
        const QRectF arc = box.adjusted(thickness / 2, thickness / 2,
                                        -thickness / 2, -thickness / 2);
        const int full = 360 * 16;
        // A 2px surface gap between the two fills, so they read as two values
        // rather than one ring that changed colour.
        const int gap = 3 * 16;
        int down_span = int(full * (double(down_) / double(total)));
        down_span = qBound(0, down_span, full);

        p.setPen(QPen(t.download, thickness, Qt::SolidLine, Qt::FlatCap));
        if (down_span > gap) p.drawArc(arc, 90 * 16 - gap / 2, -(down_span - gap));
        p.setPen(QPen(t.upload, thickness, Qt::SolidLine, Qt::FlatCap));
        const int up_span = full - down_span;
        if (up_span > gap) p.drawArc(arc, 90 * 16 - down_span - gap / 2, -(up_span - gap));
    }

    // Text wears text tokens, never a series colour - the ring beside it already
    // carries the identity.
    QFont big = font();
    big.setPointSizeF(font().pointSizeF() * 1.5);
    big.setBold(true);
    p.setFont(big);
    p.setPen(t.ink);
    p.drawText(box, Qt::AlignCenter, centerText());

    p.setFont(font());
    p.setPen(t.ink_muted);
    p.drawText(box.adjusted(0, box.height() * 0.30, 0, 0), Qt::AlignHCenter | Qt::AlignTop,
               tr("Total"));
}

}  // namespace bandsight::gui
