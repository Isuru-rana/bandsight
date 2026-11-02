// SPDX-License-Identifier: GPL-3.0-or-later
// The Apps and Hosts queries against a database the daemon's own Store wrote:
// which interface kinds count toward the per-app breakdown.
#include <QCoreApplication>
#include <QTemporaryDir>

#include "check.h"
#include "history_store.h"
#include "store.h"

using namespace bandsight;

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    CHECK(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("t.db"));

    const std::int64_t ts = 1'700'000'040;   // a minute boundary
    {
        Store s(path.toStdString());
        s.migrate();
        s.upsert_iface(2, "wlan0", "wifi");
        s.upsert_iface(1, "lo", "loopback");
        s.upsert_iface(5, "tailscale0", "vpn");
        const std::int64_t a = s.upsert_app("/usr/bin/teams", "teams", "");
        // One app, bytes on every kind of interface row.
        s.write_samples(ts, {},
                        {{a, 2, 100, 10},                  // wifi: counted
                         {a, 0, 1000, 50},                 // unknown: now counted
                         {a, 1, 5000, 5000},               // loopback: not
                         {a, 5, 300, 300},                 // vpn: not
                         {a, kIfaceOtherNetns, 7000, 7000}},  // container: not
                        {{a, 2, "1.1.1.1", 100, 10},
                         {a, 0, "2.2.2.2", 1000, 50},
                         {a, 1, "127.0.0.1", 5000, 5000},
                         {a, kIfaceOtherNetns, "172.17.0.2", 7000, 7000}});
    }

    gui::HistoryStore h;
    bool opened = false;
    QObject::connect(&h, &gui::HistoryStore::opened, [&] { opened = true; });
    h.open(path);
    CHECK(opened);

    QVector<gui::AppTotal> apps;
    QObject::connect(&h, &gui::HistoryStore::appTotals,
                     [&](const QVector<gui::AppTotal>& t) { apps = t; });
    h.queryAppTotalsRange(ts - 10, ts + 10, 1);
    CHECK(apps.size() == 1);
    if (apps.size() == 1) {
        CHECK(apps[0].rx == 1100);
        CHECK(apps[0].tx == 60);
    }

    QVector<gui::AppHostTotal> pairs;
    QObject::connect(&h, &gui::HistoryStore::hostPairs,
                     [&](const QVector<gui::AppHostTotal>& p, bool, qint64) { pairs = p; });
    h.queryHostTotalsRange(ts, ts + 59, 60);
    quint64 rx = 0, tx = 0;
    for (const auto& p : pairs) { rx += p.rx; tx += p.tx; }
    CHECK(pairs.size() == 2);
    CHECK(rx == 1100);
    CHECK(tx == 60);

    if (g_failures) std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    else std::printf("test_history_query: all checks passed\n");
    return g_failures ? 1 : 0;
}
