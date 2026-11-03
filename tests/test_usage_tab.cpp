// SPDX-License-Identifier: GPL-3.0-or-later
// The Usage tab's Unattributed rows and row tooltips, on the real widgets.
// Runs on Qt's offscreen platform (set by ctest).
#include <QApplication>

#include "check.h"
#include "tabs/usage_tab.h"
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

    if (g_failures) std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    else std::printf("test_usage_tab: all checks passed\n");
    return g_failures ? 1 : 0;
}
