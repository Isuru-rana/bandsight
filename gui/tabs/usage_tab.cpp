// SPDX-License-Identifier: GPL-3.0-or-later
#include "tabs/usage_tab.h"

#include <QHBoxLayout>
#include <QHash>
#include <QScrollArea>
#include <QComboBox>
#include <QDateTime>
#include <QLabel>
#include <QVBoxLayout>

#include <algorithm>

#include "app_labels.h"
#include "theme.h"
#include "units.h"
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
      hosts_list_(new RankedList),
      hosts_scroll_(nullptr),
      hosts_note_(new QLabel),
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

    // Same scrolling as Apps: a busy machine talks to hundreds of hosts.
    auto* hosts_scroll = new QScrollArea;
    hosts_list_->setShowChips(false);   // an IP has no initial worth showing
    hosts_scroll->setWidget(hosts_list_);
    hosts_scroll->setWidgetResizable(true);
    hosts_scroll->setFrameShape(QFrame::NoFrame);
    hosts_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    hosts_scroll->viewport()->setAutoFillBackground(false);
    hosts_scroll->setStyleSheet(QStringLiteral("background: transparent;"));

    // One line under the list for the states a reader must not misread: an old
    // daemon, an empty period, an incomplete breakdown.
    hosts_note_->setObjectName(QStringLiteral("hostsNote"));   // findChild, for tests
    hosts_note_->setWordWrap(true);
    hosts_note_->setAlignment(Qt::AlignCenter);
    hosts_note_->setStyleSheet(QStringLiteral("color:%1;").arg(theme().ink_muted.name()));
    hosts_note_->hide();

    auto* hosts_body = new QWidget;
    auto* hosts_box = new QVBoxLayout(hosts_body);
    hosts_box->setContentsMargins(0, 0, 0, 0);
    hosts_box->addWidget(hosts_scroll, 1);
    hosts_scroll_ = hosts_scroll;
    hosts_scroll_->hide();   // nothing to list until the first reply
    // Zero-factor stretches either side of the note: they take no space while
    // the list (factor 1) is shown, and centre the note while it is hidden.
    hosts_box->addStretch();
    hosts_box->addWidget(hosts_note_);
    hosts_box->addStretch();
    hosts_->setBody(hosts_body);

    // Named honestly. "Coming soon" would be a promise; this states the
    // condition, which is the thing a reader can actually check.
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
    connect(hosts_list_, &RankedList::rowClicked, this, &UsageTab::toggleHostExcluded);

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
    if (apps_->excluded().isEmpty() && hosts_list_->excluded().isEmpty()) return;
    apps_->setExcluded({});
    hosts_list_->setExcluded({});
    recomputeSplit();
}

QSet<QString> UsageTab::excluded() const { return apps_->excluded(); }

void UsageTab::toggleHostExcluded(const QString& key) {
    if (key.isEmpty()) return;
    QSet<QString> next = hosts_list_->excluded();
    if (next.contains(key)) next.remove(key);
    else next.insert(key);
    hosts_list_->setExcluded(next);
    recomputeSplit();
}

QSet<QString> UsageTab::excludedHosts() const { return hosts_list_->excluded(); }

void UsageTab::setHostPairs(const QVector<AppHostTotal>& pairs, bool collected,
                            qint64 since) {
    pairs_ = pairs;
    hosts_collected_ = collected;
    hosts_since_ = since;

    // Pairs -> one row per host. The pairs are kept: they are what lets an app
    // and a host exclusion subtract shared bytes once (recomputeSplit).
    struct Acc {
        QString ip, hostname;
        quint64 rx = 0, tx = 0;
        QVector<QPair<quint64, QString>> apps;
    };
    QHash<QString, Acc> by_key;
    for (const AppHostTotal& p : pairs) {
        Acc& a = by_key[host_key(p.ip)];
        a.ip = p.ip;
        a.hostname = p.hostname;
        a.rx += p.rx;
        a.tx += p.tx;
        AppTotal t;
        t.exe = p.exe;
        t.name = p.app_name;
        a.apps.append({p.rx + p.tx, t.display_name()});
    }

    QVector<RankedList::Row> rows;
    rows.reserve(by_key.size());
    for (auto it = by_key.begin(); it != by_key.end(); ++it) {
        Acc& a = it.value();
        std::sort(a.apps.begin(), a.apps.end(),
                  [](const auto& x, const auto& y) { return x.first > y.first; });
        // Until hostnames exist, "who is this?" is answered by who talked to it.
        QStringList top;
        for (int i = 0; i < a.apps.size() && i < 3; ++i)
            top << QStringLiteral("%1 %2").arg(a.apps[i].second, format_volume(a.apps[i].first));
        const QString addr = a.ip.isEmpty() ? tr("No peer address") : a.ip;
        rows.push_back({host_label(a.ip, a.hostname),
                        addr + QStringLiteral("\n") + top.join(QStringLiteral(" · ")),
                        a.rx + a.tx, a.rx, a.tx, it.key()});
    }
    std::sort(rows.begin(), rows.end(), [](const RankedList::Row& x, const RankedList::Row& y) {
        return x.value > y.value;
    });
    hosts_list_->setRows(rows);

    // Same rule as apps: a host that left the period cannot be clicked back, so
    // it must not keep filtering the total invisibly.
    QSet<QString> pruned;
    for (const QString& k : hosts_list_->excluded())
        if (by_key.contains(k)) pruned.insert(k);
    if (pruned.size() != hosts_list_->excluded().size()) hosts_list_->setExcluded(pruned);

    updateHostsNote();
    recomputeSplit();
}

// "Clearly short": under 90% of what the apps moved, and at least 1 MB of it, so
// minute-edge rounding on short windows never raises it. Compared against the
// APP totals, not the interfaces: both come from the same hooks, so the only
// gap between them is a full socket map - which is what the note reports.
void UsageTab::updateHostsNote() {
    QString text;
    if (!hosts_collected_) {
        text = tr("Update bandsightd to collect hosts.");
    } else if (pairs_.isEmpty()) {
        text = tr("No host traffic in this period");
    } else if (hosts_since_ > 0) {
        // Host recording began inside the period while app history is older, so
        // a shortfall is expected, not a full socket map. Say nothing.
    } else {
        quint64 apps = 0, hosts = 0;
        for (const AppTotal& t : totals_) apps += t.rx + t.tx;
        for (const AppHostTotal& p : pairs_) hosts += p.rx + p.tx;
        if (apps >= hosts + 1'000'000 && hosts * 10 < apps * 9)
            text = tr("Host breakdown incomplete");
    }
    hosts_note_->setText(text);
    hosts_note_->setVisible(!text.isEmpty());
    // An old daemon or an empty period is said once, by the note: the empty
    // list's own "Nothing in this period" beside it would be a second message.
    hosts_scroll_->setVisible(hosts_collected_ && !pairs_.isEmpty());
}

// The headline comes from the INTERFACE totals, so an exclusion is a
// subtraction from it rather than a re-sum of the rows - that is what keeps
// unattributed bytes in the figure (they belong to no application and so can
// never be excluded).
//
// Clamped at zero: the two sides are collected by different mechanisms, and a
// rounding or attribution mismatch must not render a negative total.
// Host exclusions join the subtraction as a union over (app, host) pairs.
void UsageTab::recomputeSplit() {
    const QSet<QString>& out_apps = apps_->excluded();
    const QSet<QString>& out_hosts = hosts_list_->excluded();
    quint64 sub_down = 0, sub_up = 0;
    int apps_removed = 0;
    QString one_app;
    for (const AppTotal& t : totals_) {
        if (!out_apps.contains(t.exe)) continue;
        sub_down += t.rx;
        sub_up += t.tx;
        ++apps_removed;
        one_app = t.display_name();
    }
    // The union: bytes to an excluded host, unless their app is already out -
    // those were subtracted above and must not be subtracted twice.
    for (const AppHostTotal& p : pairs_) {
        if (!out_hosts.contains(host_key(p.ip)) || out_apps.contains(p.exe)) continue;
        sub_down += p.rx;
        sub_up += p.tx;
    }
    const quint64 down = sub_down >= split_down_ ? 0 : split_down_ - sub_down;
    const quint64 up = sub_up >= split_up_ ? 0 : split_up_ - sub_up;
    donut_->setValues(down, up);
    stats_->setValues(down, up);

    // A filtered total must never look like a real one.
    const int hosts_removed = int(out_hosts.size());
    QString text;
    if (apps_removed == 1 && hosts_removed == 0) {
        text = tr("excluding %1").arg(one_app);
    } else if (apps_removed > 1 && hosts_removed == 0) {
        text = tr("excluding %n application(s)", nullptr, apps_removed);
    } else if (apps_removed == 0 && hosts_removed == 1) {
        const QString k = *out_hosts.begin();
        text = tr("excluding %1").arg(k == host_key(QString()) ? tr("No peer address") : k);
    } else if (apps_removed == 0 && hosts_removed > 1) {
        text = tr("excluding %n host(s)", nullptr, hosts_removed);
    } else if (apps_removed > 0 && hosts_removed > 0) {
        text = tr("excluding %1, %2")
                   .arg(tr("%n application(s)", nullptr, apps_removed),
                        tr("%n host(s)", nullptr, hosts_removed));
    }
    excluding_->setText(text);
    excluding_->setVisible(!text.isEmpty());
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

    updateHostsNote();   // the incomplete note compares against these totals
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
