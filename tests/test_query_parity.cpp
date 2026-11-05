// SPDX-License-Identifier: GPL-3.0-or-later
// The GUI's history queries and formatting now run on core/ (no Qt). This test
// proves nothing drifted in the move: the QtSql HistoryStore and the Qt
// formatting exactly as they were (tests/legacy/) against the current ones, on
// the same databases and inputs, for every window, a spread of ranges and
// clocks, an old-schema database and a broken one. Every payload is compared
// field by field.
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QTemporaryDir>

#include <ctime>
#include <random>

#include <sqlite3.h>

#include "check.h"
#include "core/units.h"
#include "fixture_db.h"
#include "history_store.h"
#include "legacy/legacy_history_store.h"
#include "legacy/legacy_logic.h"
#include "app_labels.h"
#include "host_class.h"
#include "service_names.h"
#include "units.h"

using namespace bandsight;
using bandsight::fixture::kFixtureNow;

namespace {

// Everything one store emits, flattened to text so a mismatch prints.
struct Capture {
    QStringList lines;
    template <class Store>
    void attach(Store& s) {
        QObject::connect(&s, &Store::opened, [this](const QString& p) { lines << "opened " + p; });
        QObject::connect(&s, &Store::openFailed,
                         [this](const QString& p, const QString&) { lines << "openFailed " + p; });
        QObject::connect(&s, &Store::series, [this](gui::Window w, const QVector<gui::SeriesPoint>& v) {
            QString l = QStringLiteral("series %1 n=%2").arg(int(w)).arg(v.size());
            for (const auto& p : v)
                if (p.rx || p.tx) l += QStringLiteral(" %1:%2/%3").arg(p.ts).arg(p.rx).arg(p.tx);
            if (!v.isEmpty()) l += QStringLiteral(" first=%1 last=%2").arg(v.front().ts).arg(v.back().ts);
            lines << l;
        });
        QObject::connect(&s, &Store::ifaceTotals, [this](const QVector<gui::IfaceTotal>& v) {
            QString l = "ifaces";
            for (const auto& t : v) {
                QStringList ids;
                for (int id : t.ids) ids << QString::number(id);
                l += QStringLiteral(" [%1 %2 %3 %4/%5 ids=%6]")
                         .arg(t.id).arg(t.name, t.kind).arg(t.rx).arg(t.tx).arg(ids.join(','));
            }
            lines << l;
        });
        QObject::connect(&s, &Store::appTotals, [this](const QVector<gui::AppTotal>& v, bool ok) {
            QString l = QStringLiteral("apps ok=%1").arg(ok);
            for (const auto& t : v)
                l += QStringLiteral(" [%1 %2 %3 %4/%5]").arg(t.id).arg(t.exe, t.name).arg(t.rx).arg(t.tx);
            lines << l;
        });
        QObject::connect(&s, &Store::hostPairs,
                         [this](const QVector<gui::AppHostTotal>& v, bool collected, qint64 since) {
            // The pairs come back in SQLite's GROUP BY order on both sides.
            QString l = QStringLiteral("hosts collected=%1 since=%2").arg(collected).arg(since);
            for (const auto& p : v)
                l += QStringLiteral(" [%1 %2 %3 %4 %5 %6 %7/%8]")
                         .arg(p.app_id).arg(p.exe, p.app_name).arg(p.host_id)
                         .arg(p.ip, p.hostname).arg(p.rx).arg(p.tx);
            lines << l;
        });
        QObject::connect(&s, &Store::typeTotals,
                         [this](const QVector<gui::TypeTotal>& v, bool collected, qint64 since, bool ok) {
            QString l = QStringLiteral("types collected=%1 since=%2 ok=%3").arg(collected).arg(since).arg(ok);
            for (const auto& t : v)
                l += QStringLiteral(" [%1 %2 %3/%4]").arg(t.proto).arg(t.port).arg(t.rx).arg(t.tx);
            lines << l;
        });
        QObject::connect(&s, &Store::quota, [this](const gui::QuotaInfo& q) {
            lines << QStringLiteral("quota used=%1 cap=%2 proj=%3 start=%4 end=%5 day=%6")
                         .arg(q.used).arg(q.cap).arg(q.projected).arg(q.cycle_start)
                         .arg(q.cycle_end).arg(q.cycle_day);
        });
    }
};

template <class Store>
void drive(Store& s, const QString& path) {
    s.open(path);
    const QSet<int> measured{2, 3};
    for (const std::int64_t now : {kFixtureNow, kFixtureNow + 37, kFixtureNow - 5 * 3600 + 11,
                                   kFixtureNow + 86400 * 2}) {
        for (gui::Window w : gui::all_windows()) {
            s.querySeries(w, measured, now);
            s.querySeries(w, QSet<int>{}, now);
            s.queryIfaceTotals(w, now);
            s.queryAppTotals(w, now);
            s.queryHostTotals(w, now);
            s.queryTypeTotals(w, now);
        }
        s.queryQuota(now);
    }
    // Ranges as the timeline hands them over, at every tier, including ones that
    // start or end mid-bucket and one past the end of the data.
    const std::int64_t n = kFixtureNow;
    const struct { std::int64_t from, to; int res; } ranges[] = {
        {n - 300, n, 1},         {n - 1800 + 7, n - 61, 1}, {n - 3600, n, 60},
        {n - 6 * 3600 + 30, n - 4 * 3600 - 30, 60},          {n - 86400, n, 60},
        {n - 7 * 86400, n, 3600}, {n - 30 * 86400, n, 3600},  {n - 25 * 86400, n - 5 * 86400, 3600},
        {n - 30 * 86400, n, 86400}, {n + 100, n + 1000, 60},  {n - 45, n, 1},
    };
    for (const auto& r : ranges) {
        s.queryIfaceTotalsRange(r.from, r.to, r.res);
        s.queryAppTotalsRange(r.from, r.to, r.res);
        s.queryHostTotalsRange(r.from, r.to, r.res);
        s.queryTypeTotalsRange(r.from, r.to, r.res);
    }
}

void compare(const QString& path, const char* what) {
    gui::HistoryStore now_store;
    legacy::LegacyHistoryStore old_store;
    Capture a, b;
    a.attach(now_store);
    b.attach(old_store);
    drive(now_store, path);
    drive(old_store, path);
    CHECK(a.lines.size() == b.lines.size());
    int shown = 0;
    for (int i = 0; i < std::min(a.lines.size(), b.lines.size()); ++i) {
        if (a.lines[i] == b.lines[i]) continue;
        CHECK(a.lines[i] == b.lines[i]);
        if (shown++ < 3)
            std::fprintf(stderr, "%s, reply %d:\n  new: %s\n  old: %s\n", what, i,
                         qPrintable(a.lines[i].left(600)), qPrintable(b.lines[i].left(600)));
    }
    std::fprintf(stderr, "%s: %lld replies compared\n", what, (long long)a.lines.size());
}

void exec(const QString& path, const char* sql) {
    sqlite3* db = nullptr;
    CHECK(sqlite3_open(path.toUtf8().constData(), &db) == SQLITE_OK);
    CHECK(sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(db);
}

void queries(const QTemporaryDir& dir) {
    const QString path = dir.filePath("fixture.db");
    fixture::write(path.toStdString());
    compare(path, "fixture");

    // A database a v2 daemon wrote: no type_samples, no iface_id on the host rows.
    const QString old = dir.filePath("v2.db");
    QFile::copy(path, old);
    exec(old,
         "DROP TABLE type_samples;"
         "CREATE TABLE h2 AS SELECT res, ts, app_id, host_id, rx, tx FROM app_host_samples;"
         "DROP TABLE app_host_samples; ALTER TABLE h2 RENAME TO app_host_samples;");
    compare(old, "old schema");

    // A file that opens but is not a database: every query fails.
    const QString junk = dir.filePath("junk.db");
    {
        QFile f(junk);
        CHECK(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(4096, 'x'));
    }
    compare(junk, "not a database");

    // A missing one never opens.
    compare(dir.filePath("missing.db"), "missing");
}

void formatting() {
    std::mt19937_64 rng(5);
    std::vector<quint64> values;
    for (quint64 v = 0; v < 200000; ++v) values.push_back(v);
    for (quint64 v = 0; v < 200000; ++v) values.push_back(v * 125);   // exact ties
    for (int i = 0; i < 200000; ++i) values.push_back(rng() >> (i % 44));
    for (quint64 p = 1; p < (1ull << 62); p *= 10)
        for (quint64 d : {p - 1, p, p + 1, p * 5 / 10, p * 95 / 100, p * 9995 / 10000})
            values.push_back(d);

    int bad = 0;
    for (auto rate : {gui::RateUnit::Bits, gui::RateUnit::Bytes}) {
        for (auto vol : {gui::VolumeUnit::Decimal, gui::VolumeUnit::Binary}) {
            gui::set_rate_unit(rate);
            gui::set_volume_unit(vol);
            legacy::set_rate_unit(rate);
            legacy::set_volume_unit(vol);
            for (quint64 v : values) {
                if (gui::format_rate(v) != legacy::format_rate(v) ||
                    gui::format_volume(v) != legacy::format_volume(v)) {
                    if (bad++ < 5)
                        std::fprintf(stderr, "format %llu: %s / %s vs %s / %s\n", (unsigned long long)v,
                                     qPrintable(gui::format_rate(v)), qPrintable(gui::format_volume(v)),
                                     qPrintable(legacy::format_rate(v)),
                                     qPrintable(legacy::format_volume(v)));
                }
            }
        }
    }
    CHECK(bad == 0);
    gui::set_rate_unit(gui::RateUnit::Bits);
    gui::set_volume_unit(gui::VolumeUnit::Decimal);

    // The fixed-point rule on its own, against Qt, where printf would differ.
    CHECK(core::format_fixed(1.25, 1) == "1.3" && core::format_fixed(0.15, 1) == "0.1");
    CHECK(core::format_fixed(9.995, 2) == QString::number(9.995, 'f', 2).toStdString());
    CHECK(core::format_fixed(999.95, 1) == QString::number(999.95, 'f', 1).toStdString());
}

void names_and_zones() {
    for (int proto : {6, 17, 1})
        for (int port = 0; port <= 65535; ++port)
            if (gui::service_name(proto, port) != legacy::service_name(proto, port) ||
                gui::port_label(proto, port) != legacy::port_label(proto, port)) {
                CHECK(gui::service_name(proto, port) == legacy::service_name(proto, port));
                break;
            }

    const char* addrs[] = {
        "", "not an address", "10.0.0.1", "9.255.255.255", "11.0.0.0", "172.15.255.255",
        "172.16.0.0", "172.31.255.255", "172.32.0.0", "192.167.255.255", "192.168.0.0",
        "192.169.0.0", "169.254.0.1", "169.255.0.1", "223.255.255.255", "224.0.0.0",
        "239.255.255.255", "240.0.0.1", "255.255.255.255", "0.0.0.0", "127.0.0.1",
        "fe80::1", "fe80::1%eth0", "febf::1", "fec0::1", "fc00::1", "fdff::1", "fe00::1",
        "ff02::fb", "ff00::", "feff::1", "::1", "::", "2606:4700::1111", "::ffff:10.0.0.1",
        "::ffff:8.8.8.8", "2001:db8::1", "1.2.3", "1.2.3.4.5", "256.1.1.1", " 10.0.0.1",
        "10.0.0.1 ", "fe80::1%", "[::1]", "0x0a.0.0.1", "010.0.0.1", "1.1.1.1/8",
    };
    for (const char* a : addrs) {
        const QString ip = QString::fromLatin1(a);
        if (gui::host_zone(ip) != legacy::host_zone(ip)) {
            CHECK(gui::host_zone(ip) == legacy::host_zone(ip));
            std::fprintf(stderr, "zone differs for '%s': new %d old %d\n", a,
                         int(gui::host_zone(ip)), int(legacy::host_zone(ip)));
        }
    }
    // Every address the fixture stores, and a sweep of random ones.
    std::mt19937 rng(9);
    for (int i = 0; i < 20000; ++i) {
        const quint32 v4 = rng();
        const QString a4 = QHostAddress(v4).toString();
        CHECK(gui::host_zone(a4) == legacy::host_zone(a4));
        Q_IPV6ADDR b{};
        for (auto& c : b.c) c = quint8(rng());
        if (i % 3 == 0) { b.c[0] = 0xfe; b.c[1] = quint8(0x80 | (rng() & 0x7f)); }
        const QString a6 = QHostAddress(b).toString();
        CHECK(gui::host_zone(a6) == legacy::host_zone(a6));
    }
}

void labels() {
    const auto app = [](const char* exe, const char* name) {
        gui::AppTotal t;
        t.exe = QString::fromLatin1(exe);
        t.name = QString::fromLatin1(name);
        return t;
    };
    const QVector<QVector<gui::AppTotal>> cases{
        {app("/usr/bin/node", ""), app("/usr/local/bin/node", ""), app("/usr/bin/curl", "")},
        {app("/home/a/.nvm/versions/node/v24.18.0/bin/node", ""),
         app("/home/b/.nvm/versions/node/v24.18.0/bin/node", ""), app("", ""), app("", "")},
        {app("node", ""), app("/x/node", ""), app("/usr/bin/", ""), app("//usr//bin//x", "x")},
        {app("/opt/a/app", "Same"), app("/opt/b/app", "Same"), app("/opt/b/c/app", "Same")},
    };
    for (const auto& c : cases) CHECK(gui::disambiguated_labels(c) == legacy::disambiguated_labels(c));
}

}  // namespace

int main(int argc, char** argv) {
    qputenv("TZ", "UTC");
    tzset();
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    CHECK(dir.isValid());
    queries(dir);
    formatting();
    names_and_zones();
    labels();
    return g_failures == 0 ? 0 : 1;
}
