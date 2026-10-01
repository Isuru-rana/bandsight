// SPDX-License-Identifier: GPL-3.0-or-later
#include "quota_bar.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>

#include "units.h"

namespace bandsight::gui {

namespace {
// Spec §7 tints the tray amber above 80% of the cap and red above 100%. The bar
// uses the same two thresholds so the two never disagree about the state.
constexpr int kWarnPercent = 80;

QString cycle_end_label(qint64 end) {
    return QDateTime::fromSecsSinceEpoch(end).date().toString(QStringLiteral("d MMM"));
}
}  // namespace

QuotaBar::QuotaBar(QWidget* parent)
    : QWidget(parent), label_(new QLabel(this)), bar_(new QProgressBar(this)) {
    setObjectName(QStringLiteral("quotaBar"));
    bar_->setTextVisible(false);
    bar_->setRange(0, 100);
    bar_->setFixedHeight(10);

    auto* box = new QHBoxLayout(this);
    box->setContentsMargins(4, 2, 4, 2);
    box->addWidget(bar_, 1);
    box->addWidget(label_);

    setQuota({});
}

QString QuotaBar::text() const { return label_->text(); }

int QuotaBar::percent() const {
    if (info_.cap == 0) return -1;
    return int((info_.used * 100) / info_.cap);
}

void QuotaBar::setQuota(const QuotaInfo& info) {
    info_ = info;

    if (info_.cap == 0) {
        // No cap configured. Showing an empty or full gauge would both be
        // claims about a target that does not exist, so the bar goes away and
        // the usage is still reported - it is useful on its own.
        bar_->setVisible(false);
        label_->setText(tr("%1 this cycle · since %2")
                            .arg(format_volume(info_.used),
                                 cycle_end_label(info_.cycle_start)));
        return;
    }

    bar_->setVisible(true);
    const int pct = percent();
    bar_->setValue(qMin(pct, 100));

    // The projection is the actionable half: "you are at 40%" is comfortable
    // right up until it means "and on track for 130%".
    label_->setText(tr("%1 of %2 · %3 projected by %4")
                        .arg(format_volume(info_.used), format_volume(info_.cap),
                             format_volume(info_.projected),
                             cycle_end_label(info_.cycle_end)));

    QString chunk;
    if (pct >= 100) chunk = QStringLiteral("#c0392b");        // over cap
    else if (pct >= kWarnPercent) chunk = QStringLiteral("#d68910");
    bar_->setStyleSheet(chunk.isEmpty()
                            ? QString()
                            : QStringLiteral("QProgressBar::chunk{background:%1;}").arg(chunk));
}

}  // namespace bandsight::gui
