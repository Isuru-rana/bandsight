// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgets/identity_chip.h"

#include <QFontMetrics>
#include <QPainterPath>

#include "theme.h"

namespace bandsight::gui {

QString chip_text(const QString& name) {
    for (const QChar c : name)
        if (c.isLetterOrNumber()) return QString(c.toUpper());
    return QStringLiteral("·");   // never an empty chip
}

QColor chip_color(const QString& name) {
    const QVector<QColor> chips = theme().chips;
    if (chips.isEmpty()) return QColor(Qt::gray);
    // qHash is seeded per process by default, which would give the same
    // application a different colour in each run; this is a fixed sum instead.
    quint32 h = 0;
    for (const QChar c : name) h = h * 31u + c.unicode();
    return chips[int(h % quint32(chips.size()))];
}

void paint_identity_chip(QPainter& p, const QRect& box, const QString& name) {
    const Theme t = theme();
    p.save();
    p.setRenderHint(QPainter::Antialiasing);

    QPainterPath path;
    path.addRoundedRect(QRectF(box), Theme::radius_bar + 1, Theme::radius_bar + 1);
    p.fillPath(path, chip_color(name));

    QFont f = p.font();
    f.setBold(true);
    f.setPixelSize(qMax(8, int(box.height() * 0.6)));
    p.setFont(f);
    p.setPen(t.chip_ink);
    p.drawText(box, Qt::AlignCenter, chip_text(name));
    p.restore();
}

}  // namespace bandsight::gui
