// SPDX-License-Identifier: GPL-3.0-or-later
// The Usage tab's Unattributed rows, row tooltips and Traffic type card, on
// the real widgets.
// Runs on Qt's offscreen platform (set by ctest).
#include <QApplication>
#include <QLabel>
#include <QMouseEvent>

#include "check.h"
#include "tabs/usage_tab.h"
#include "widgets/card.h"
#include "widgets/ranked_list.h"

using namespace bandsight::gui;

namespace {

AppTotal app(int id, const char* exe, const char* name, quint64 rx, quint64 tx) {
    AppTotal t;
    t.id = id;
    t.exe = QString::fromLatin1(exe);
    t.name = QString::fromLatin1(name);
    t.rx = rx;
    t.tx = tx;
    return t;
}

// The Apps list is the one whose rows carry executables as keys.
RankedList* apps_list(UsageTab& tab) {
    for (RankedList* l : tab.findChildren<RankedList*>())
        for (const auto& r : l->rows())
            if (r.key.startsWith(QLatin1Char('/'))) return l;
    return nullptr;
}

TypeTotal type(int proto, int port, quint64 rx, quint64 tx) {
    TypeTotal t;
    t.proto = proto;
    t.port = port;
    t.rx = rx;
    t.tx = tx;
    return t;
}

quint64 footer_value(RankedList* l) {
    return l && l->footerRow() ? l->footerRow()->value : 0;
}

}  // namespace

int main(int argc, char** argv) {
    QApplication qapp(argc, argv);
    UsageTab tab;
    tab.resize(960, 470);

    const QVector<AppTotal> apps{app(1, "/usr/bin/claude", "claude", 60'000'000, 31'000'000),
                                 app(2, "/opt/brave/brave", "brave", 50'000'000, 5'500'000)};

    // The reported case: the rows cover a fraction of the headline.
    tab.setTotals(apps);
    tab.setSplit(1'120'000'000, 168'600'000);
    RankedList* list = apps_list(tab);
    CHECK(list != nullptr);
    CHECK(footer_value(list) == 1'120'000'000 - 110'000'000 + 168'600'000 - 36'500'000);
    CHECK(list && list->footerRow() && list->footerRow()->key.isEmpty());   // not excludable

    // Rows covering the headline: no remainder row.
    tab.setSplit(110'000'000, 36'500'000);
    CHECK(list && !list->footerRow());

    // A failed app query is not "nothing attributed": no row of the whole total.
    tab.setSplit(1'120'000'000, 168'600'000);
    CHECK(list && list->footerRow());
    tab.setTotals({}, false);
    CHECK(list && !list->footerRow());

    // While a refresh is outstanding, the new headline is not compared with the
    // previous period's rows: the row keeps its value until both have arrived.
    tab.setTotals(apps);
    const quint64 before = footer_value(list);
    CHECK(before > 0);
    tab.beginRefresh();
    tab.setSplit(40'000'000'000, 2'000'000'000);   // a 30-day headline arrives first
    CHECK(footer_value(list) == before);
    tab.setTotals(apps);                            // its app list arrives
    CHECK(footer_value(list) == 40'000'000'000 - 110'000'000 + 2'000'000'000 - 36'500'000);
    tab.setHostPairs({}, true, 0);                  // and its (empty) host list

    // Two refreshes in flight: the first one's replies must not release the
    // second one's wait.
    tab.beginRefresh();
    tab.beginRefresh();
    tab.setSplit(1'120'000'000, 168'600'000);       // refresh 1
    tab.setTotals(apps);
    tab.setHostPairs({}, true, 0);
    const quint64 after_first = footer_value(list);
    tab.setSplit(5'000'000'000, 500'000'000);       // refresh 2, headline only
    CHECK(footer_value(list) == after_first);
    tab.setTotals(apps);
    CHECK(footer_value(list) == 5'000'000'000 - 110'000'000 + 500'000'000 - 36'500'000);
    tab.setHostPairs({}, true, 0);

    // Every row's tooltip opens with its full label.
    tab.show();
    qapp.processEvents();
    if (list && !list->rows().isEmpty()) {
        const QString tip = list->tooltipAt(QPoint(5, 5));
        CHECK(tip.startsWith(list->rows().first().label + QLatin1Char('\n')));
        CHECK(tip.contains(QStringLiteral("/usr/bin/claude")));
    } else {
        CHECK(false);
    }

    // Traffic type: one row per service name, largest first, never clickable.
    RankedList* types = tab.traffic();
    CHECK(types != nullptr);
    tab.setTypeTotals({type(6, 443, 900, 100), type(17, 443, 400, 50), type(17, 53, 10, 5),
                       type(6, 53, 3, 2), type(17, 0, 70, 30)},
                      true, 0);
    qapp.processEvents();
    if (types) {
        const auto& rows = types->rows();
        CHECK(rows.size() == 4);
        if (rows.size() == 4) {
            CHECK(rows[0].label == QLatin1String("HTTPS") && rows[0].value == 1000);
            CHECK(rows[1].label == QLatin1String("QUIC") && rows[1].down == 400 &&
                  rows[1].up == 50);
            CHECK(rows[2].label == QLatin1String("Other") && rows[2].value == 100);
            CHECK(rows[3].label == QLatin1String("DNS") && rows[3].value == 20);   // tcp + udp
            CHECK(rows[3].tooltip.contains(QLatin1String("53/udp")) &&
                  rows[3].tooltip.contains(QLatin1String("53/tcp")));
        }
        for (const auto& r : rows) CHECK(r.key.isEmpty());
        CHECK(types->isVisible());
        int clicks = 0;
        QObject::connect(types, &RankedList::rowClicked, [&] { ++clicks; });
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(5, 5), types->mapToGlobal(QPointF(5, 5)),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(types, &press);
        CHECK(clicks == 0);
        CHECK(types->rowAt(QPoint(5, 5)) == 0);   // the click did land on a row
    }
    CHECK(tab.trafficNote() && tab.trafficNote()->isHidden());
    CHECK(tab.trafficCard() && !tab.trafficCard()->isReserved());

    // A period that starts before type recording: the list plus a note.
    tab.setTypeTotals({type(6, 443, 1, 1)}, true, 1'700'000'000);
    CHECK(tab.trafficNote()->isVisible());
    CHECK(tab.trafficNote()->text().startsWith(QLatin1String("Traffic types recorded since")));
    CHECK(types && types->isVisible());

    // An old daemon: no list, a note saying what to do.
    tab.setTypeTotals({}, false, 0);
    CHECK(tab.trafficNote()->isVisible());
    CHECK(tab.trafficNote()->text().contains(QLatin1String("Update bandsightd")));
    CHECK(types && !types->isVisible());

    // An empty period.
    tab.setTypeTotals({}, true, 0);
    CHECK(tab.trafficNote()->text() == QLatin1String("No traffic in this period"));

    if (g_failures) std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    else std::printf("test_usage_tab: all checks passed\n");
    return g_failures ? 1 : 0;
}
