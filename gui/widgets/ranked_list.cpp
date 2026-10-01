// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgets/ranked_list.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

#include "theme.h"
#include "widgets/identity_chip.h"
#include "units.h"

namespace bandsight::gui {

namespace {
constexpr int kBarHeight = 4;
constexpr int kRowPad = 10;
constexpr int kChipGap = 8;
}  // namespace

RankedList::RankedList(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);   // the tooltip is the hover layer
    setCursor(Qt::PointingHandCursor);
}

void RankedList::setExcluded(const QSet<QString>& keys) {
    excluded_ = keys;
    update();
}

bool RankedList::isExcluded(int row) const {
    return row >= 0 && row < rows_.size() && excluded_.contains(rows_[row].key);
}

int RankedList::rowAt(const QPoint& pos) const {
    const int h = rowHeight();
    if (h <= 0) return -1;
    const int i = pos.y() / h;
    return (i >= 0 && i < rows_.size()) ? i : -1;
}

void RankedList::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    const int i = rowAt(e->position().toPoint());
    // A row with no key cannot be excluded and put back reliably, so it is not
    // clickable rather than silently doing nothing on the second click.
    if (i < 0 || rows_[i].key.isEmpty()) return;
    emit rowClicked(rows_[i].key);
}

void RankedList::setRows(const QVector<Row>& rows) {
    rows_ = rows;
    updateGeometry();
    update();
}

quint64 RankedList::maxValue() const {
    quint64 m = 0;
    for (const Row& r : rows_) m = qMax(m, r.value);
    return m;
}

QStringList RankedList::visibleLabels() const {
    QStringList out;
    for (const Row& r : rows_) out << r.label;
    return out;
}

int RankedList::rowHeight() const {
    return fontMetrics().height() + kBarHeight + kRowPad;
}

// Square, and sized from the text so it tracks the font rather than a constant
// that only looks right at one scale.
int RankedList::chipSize() const { return fontMetrics().height(); }

QSize RankedList::sizeHint() const {
    return {220, qMax(1, int(rows_.size())) * rowHeight()};
}

void RankedList::paintEvent(QPaintEvent*) {
    const Theme t = theme();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    if (rows_.isEmpty()) {
        p.setPen(t.ink_muted);
        p.drawText(rect(), Qt::AlignCenter, tr("Nothing in this period"));
        return;
    }

    const quint64 max = maxValue();
    const int rh = rowHeight();
    const int text_h = fontMetrics().height();

    for (int i = 0; i < rows_.size(); ++i) {
        const Row& r = rows_[i];
        const int y = i * rh;
        if (y > height()) break;

        const QString value = format_volume(r.value);
        const int value_w = fontMetrics().horizontalAdvance(value) + 8;
        const bool out = excluded_.contains(r.key);

        // Struck through and dimmed rather than removed: the row is filtered out
        // of a total, not gone, and it has to stay clickable to come back.
        QFont row_font = font();
        row_font.setStrikeOut(out);
        p.setFont(row_font);

        // The chip goes first, so the eye has a fixed left edge to run down.
        const int chip = chipSize();
        const QRect chip_box(0, y, chip, chip);
        if (out) p.setOpacity(0.35);   // filtered rows recede, chip and all
        paint_identity_chip(p, chip_box, r.label);
        if (out) p.setOpacity(1.0);

        const int text_x = chip + kChipGap;
        const int text_w = width() - value_w - text_x;

        p.setPen(out ? t.ink_faint : t.ink);
        // The name is elided, never wrapped: a wrapped row breaks the constant
        // row height the eye is using to scan the column.
        p.drawText(QRect(text_x, y, text_w, text_h), Qt::AlignVCenter | Qt::AlignLeft,
                   fontMetrics().elidedText(r.label, Qt::ElideRight, text_w));
        p.setPen(out ? t.ink_faint : t.ink_muted);
        p.drawText(QRect(width() - value_w, y, value_w, text_h),
                   Qt::AlignVCenter | Qt::AlignRight, value);
        p.setFont(font());

        // Proportion bar, scaled to the largest row. Rounded data-end, flat
        // start: it grows from a baseline rather than floating.
        const int bar_y = y + text_h + 3;
        const double frac = max ? double(r.value) / double(max) : 0.0;
        // Aligned with the label rather than the chip, so the bars form one
        // straight edge down the column.
        const int bar_x = text_x;
        const int full_w = width() - bar_x;
        p.setPen(Qt::NoPen);
        p.setBrush(t.track);
        p.drawRoundedRect(QRect(bar_x, bar_y, full_w, kBarHeight),
                          Theme::radius_bar, Theme::radius_bar);
        const int w = int(full_w * frac);
        if (w > 0) {
            // Split into the two directions rather than painted one colour.
            // Blue means download everywhere else on this screen, so a total
            // wearing blue would make the colour lie - and the composition is
            // worth seeing: an application that only uploads looks nothing like
            // one that only downloads at the same total.
            const quint64 pair = r.down + r.up;
            const int down_w = pair ? int(w * (double(r.down) / double(pair))) : w;
            // The bar keeps its length (an excluded row still compares the same
            // way) but loses its colour, so the eye can see at a glance which
            // rows are counted.
            p.setBrush(out ? t.track : t.download);
            p.drawRoundedRect(QRect(bar_x, bar_y, qMax(down_w, Theme::radius_bar * 2),
                                    kBarHeight),
                              Theme::radius_bar, Theme::radius_bar);
            const int up_w = w - down_w;
            if (up_w > 0) {
                p.setBrush(out ? t.track : t.upload);
                // A surface gap so the two read as two values, not one bar that
                // changed colour part way along.
                p.drawRoundedRect(QRect(bar_x + down_w + Theme::gap, bar_y,
                                        qMax(up_w - Theme::gap, Theme::radius_bar * 2),
                                        kBarHeight),
                                  Theme::radius_bar, Theme::radius_bar);
            }
        }
    }
}

void RankedList::mouseMoveEvent(QMouseEvent* e) {
    const int i = e->position().y() / qMax(1, rowHeight());
    if (i >= 0 && i < rows_.size() && !rows_[i].tooltip.isEmpty())
        QToolTip::showText(e->globalPosition().toPoint(), rows_[i].tooltip, this);
    else
        QToolTip::hideText();
}

}  // namespace bandsight::gui
