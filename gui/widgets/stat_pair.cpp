// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgets/stat_pair.h"

#include <QPainter>

#include "theme.h"
#include "units.h"

namespace bandsight::gui {

namespace {
constexpr int kBarW = 26;
constexpr int kBarMaxH = 54;
}  // namespace

StatPair::StatPair(QWidget* parent) : QWidget(parent) {}

void StatPair::setValues(quint64 down, quint64 up) {
    down_ = down;
    up_ = up;
    update();
}

QString StatPair::downText() const { return format_volume(down_); }
QString StatPair::upText() const { return format_volume(up_); }

QSize StatPair::sizeHint() const {
    return {180, kBarMaxH + fontMetrics().height() * 2 + 14};
}

void StatPair::paintEvent(QPaintEvent*) {
    const Theme t = theme();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const quint64 max = qMax(down_, up_);
    const int text_h = fontMetrics().height();
    const int base = kBarMaxH;

    struct Col { quint64 v; QColor c; QString caption; };
    const Col cols[2] = {{down_, t.download, tr("Incoming")},
                         {up_, t.upload, tr("Outgoing")}};

    const int half = width() / 2;
    for (int i = 0; i < 2; ++i) {
        const int cx = half / 2 + i * half;
        // Scaled against the larger of the two, never against the cap or the
        // widget: this pair answers "how do these compare", nothing else.
        const int h = max ? int(kBarMaxH * (double(cols[i].v) / double(max))) : 0;
        p.setPen(Qt::NoPen);
        p.setBrush(cols[i].c);
        if (h > 0)
            p.drawRoundedRect(QRect(cx - kBarW / 2, base - h, kBarW, h),
                              Theme::radius_bar, Theme::radius_bar);

        p.setPen(t.ink_muted);
        p.drawText(QRect(cx - half / 2, base + 4, half, text_h),
                   Qt::AlignCenter, cols[i].caption);
        p.setPen(t.ink);
        QFont f = font();
        f.setBold(true);
        p.setFont(f);
        p.drawText(QRect(cx - half / 2, base + 4 + text_h, half, text_h),
                   Qt::AlignCenter, format_volume(cols[i].v));
        p.setFont(font());
    }
}

}  // namespace bandsight::gui
