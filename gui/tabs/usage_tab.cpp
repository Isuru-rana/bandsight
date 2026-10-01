// SPDX-License-Identifier: GPL-3.0-or-later
#include "tabs/usage_tab.h"

#include <QHBoxLayout>
#include <QScrollArea>
#include <QComboBox>
#include <QDateTime>
#include <QLabel>
#include <QVBoxLayout>

#include "app_labels.h"
#include "theme.h"
#include "widgets/card.h"
#include "widgets/donut_chart.h"
#include "widgets/ranked_list.h"
#include "widgets/ranked_list.h"
#include "widgets/stat_pair.h"
#include "widgets/timeline_strip.h"

namespace bandsight::gui {

UsageTab::UsageTab(QWidget* parent)
    : QWidget(parent),
      donut_(new DonutChart),
      stats_(new StatPair),
      apps_(new RankedList),
      timeline_(new TimelineStrip),
      hosts_(new Card(tr("Hosts"))),
      traffic_(new Card(tr("Traffic type"))),
      period_(new QLabel(this)),
      excluding_(new QLabel(this)),
      window_(new QComboBox(this)) {
    auto* total = new Card(tr("Total"));
    auto* total_body = new QWidget;
    auto* total_box = new QVBoxLayout(total_body);
    total_box->setContentsMargins(0, 0, 0, 0);
    total_box->addWidget(donut_, 1);
    total_box->addWidget(stats_);
    total->setBody(total_body);

    // The list is as long as the machine is busy - 161 applications have been
    // recorded here. Without this the rows past the card's height are simply not
    // drawn, and nothing on screen says there are more.
    auto* apps_scroll = new QScrollArea;
    apps_scroll->setWidget(apps_);
    apps_scroll->setWidgetResizable(true);
    apps_scroll->setFrameShape(QFrame::NoFrame);
    apps_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    apps_scroll->viewport()->setAutoFillBackground(false);
    apps_scroll->setStyleSheet(QStringLiteral("background: transparent;"));

    auto* apps_card = new Card(tr("Apps"));
    apps_card->setBody(apps_scroll);

    // Named honestly. "Coming soon" would be a promise; this states the
    // condition, which is the thing a reader can actually check.
    hosts_->setReserved(tr("Needs per-connection collection.\nThe daemon does not "
                           "record peer addresses yet."));
    traffic_->setReserved(tr("Needs port and protocol collection.\nThe daemon does "
                             "not record them yet."));

    auto* columns = new QHBoxLayout;
    columns->setSpacing(10);
    columns->addWidget(total, 3);
    columns->addWidget(apps_card, 4);
    columns->addWidget(hosts_, 4);
    columns->addWidget(traffic_, 4);

    auto* timeline_card = new Card(tr("Over time"));
    timeline_card->setBody(timeline_);

    QFont pf = period_->font();
    pf.setBold(true);
    period_->setFont(pf);
    period_->setAlignment(Qt::AlignCenter);

    excluding_->setObjectName(QStringLiteral("excludingNote"));   // findChild, for tests
    excluding_->setAlignment(Qt::AlignCenter);
    excluding_->hide();

    // Clicking a row filters it out of the total above. Handled here rather than
    // in the list, because the subtraction is this screen's business.
    connect(apps_, &RankedList::rowClicked, this, &UsageTab::toggleExcluded);

    for (Window w : all_windows()) window_->addItem(window_label(w), int(w));
    window_->setCurrentIndex(window_->findData(int(Window::FiveMin)));
    // The control that changes the period now sits on the screen the period
    // applies to. It used to live on the Graph tab, one tab away from every
    // number it governed - the same coupling U1 documents for the Interfaces
    // totals, and worse here because this whole screen is totals.
    connect(window_, &QComboBox::currentIndexChanged, this, [this] {
        emit windowChangeRequested(window());
    });

    auto* header = new QHBoxLayout;
    header->addStretch();
    header->addWidget(period_);
    header->addWidget(excluding_);
    header->addStretch();
    header->addWidget(window_);

    auto* box = new QVBoxLayout(this);
    box->setContentsMargins(10, 10, 10, 10);
    box->setSpacing(10);
    box->addLayout(header);
    box->addLayout(columns, 1);
    box->addWidget(timeline_card);
}

void UsageTab::toggleExcluded(const QString& exe) {
    if (exe.isEmpty()) return;
    QSet<QString> next = apps_->excluded();
    if (next.contains(exe)) next.remove(exe);
    else next.insert(exe);
    apps_->setExcluded(next);
    recomputeSplit();
}

void UsageTab::clearExcluded() {
    if (apps_->excluded().isEmpty()) return;
    apps_->setExcluded({});
    recomputeSplit();
}

QSet<QString> UsageTab::excluded() const { return apps_->excluded(); }

// The headline comes from the INTERFACE totals, so an exclusion is a
// subtraction from it rather than a re-sum of the rows - that is what keeps
// unattributed bytes in the figure (they belong to no application and so can
// never be excluded).
//
// Clamped at zero: the two sides are collected by different mechanisms, and a
// rounding or attribution mismatch must not render a negative total.
void UsageTab::recomputeSplit() {
    const QSet<QString>& out = apps_->excluded();
    quint64 down = split_down_, up = split_up_;
    int removed = 0;
    for (const AppTotal& t : totals_) {
        if (!out.contains(t.exe)) continue;
        down = (t.rx >= down) ? 0 : down - t.rx;
        up = (t.tx >= up) ? 0 : up - t.tx;
        ++removed;
    }
    donut_->setValues(down, up);
    stats_->setValues(down, up);

    // A filtered total must never look like a real one.
    if (removed == 0) {
        excluding_->hide();
    } else if (removed == 1) {
        for (const AppTotal& t : totals_) {
            if (!out.contains(t.exe)) continue;
            excluding_->setText(tr("excluding %1").arg(t.display_name()));
            break;
        }
        excluding_->show();
    } else {
        excluding_->setText(tr("excluding %n application(s)", nullptr, removed));
        excluding_->show();
    }
}

void UsageTab::setTotals(const QVector<AppTotal>& totals) {
    totals_ = totals;
    // Labels are disambiguated only where they collide (U4): four different node
    // binaries otherwise render as four rows called "node", with the only thing
    // telling them apart hidden in a tooltip.
    const QStringList labels = disambiguated_labels(totals);

    QVector<RankedList::Row> rows;
    rows.reserve(totals.size());
    for (int i = 0; i < totals.size(); ++i) {
        const AppTotal& t = totals[i];
        rows.push_back({labels.value(i, t.display_name()), t.exe, t.rx + t.tx,
                        t.rx, t.tx, t.exe});
    }
    apps_->setRows(rows);

    // An application that has left the window entirely cannot be clicked back,
    // so it must not keep filtering the total invisibly.
    QSet<QString> still_here;
    for (const AppTotal& t : totals) still_here.insert(t.exe);
    QSet<QString> pruned;
    for (const QString& e : apps_->excluded())
        if (still_here.contains(e)) pruned.insert(e);
    if (pruned.size() != apps_->excluded().size()) apps_->setExcluded(pruned);

    recomputeSplit();
}

void UsageTab::setSplit(quint64 down, quint64 up) {
    split_down_ = down;
    split_up_ = up;
    recomputeSplit();
}

void UsageTab::setPeriod(const QString& label) {
    period_->setText(tr("Everything below covers the last %1").arg(label));
}

void UsageTab::setPeriodRange(qint64 from, qint64 to) {
    const auto fmt = [](qint64 ts) {
        return QDateTime::fromSecsSinceEpoch(ts).toString(QStringLiteral("d MMM HH:mm"));
    };
    period_->setText(tr("Everything below covers %1 – %2 (selected)")
                         .arg(fmt(from), fmt(to)));
}

QString UsageTab::period() const { return period_->text(); }

Window UsageTab::window() const {
    return static_cast<Window>(window_->currentData().toInt());
}

void UsageTab::setWindow(Window w) {
    const int i = window_->findData(int(w));
    if (i < 0 || i == window_->currentIndex()) return;
    // Blocked so reflecting the application's window does not look like the user
    // asking to change it, which would bounce straight back to MainWindow.
    const QSignalBlocker block(window_);
    window_->setCurrentIndex(i);
}

void UsageTab::setSeries(const QVector<SeriesPoint>& points) {
    timeline_->setSeries(points);
}

}  // namespace bandsight::gui
