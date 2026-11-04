// SPDX-License-Identifier: GPL-3.0-or-later
// GUI logic with no widgets: the data-cap cycle and its usage query, the live
// socket's frame decoding, and LAN/WAN classification.
#include <QCoreApplication>
#include <QTemporaryDir>

#include <sqlite3.h>

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <string>

#include "check.h"
#include "frame_codec.h"
#include "history_store.h"
#include "json_frame.h"
#include "host_class.h"
#include "quota.h"
#include "store.h"
#include "time_buckets.h"

using namespace bandsight;
using namespace bandsight::gui;

namespace {

// Local midnight, in whatever TZ is set.
qint64 midnight(int y, int m, int d) {
    std::tm tm{};
    tm.tm_year = y - 1900;
    tm.tm_mon = m - 1;
    tm.tm_mday = d;
    tm.tm_isdst = -1;
    return qint64(std::mktime(&tm));
}

void cycles() {
    // Renewing on the 5th: the 10th is in this month's cycle, the 3rd in last.
    Cycle c = cycle_for(midnight(2026, 3, 10) + 3600, 5);
    CHECK(c.start == midnight(2026, 3, 5));
    CHECK(c.end == midnight(2026, 4, 5));
    c = cycle_for(midnight(2026, 3, 3) + 3600, 5);
    CHECK(c.start == midnight(2026, 2, 5));
    CHECK(c.end == midnight(2026, 3, 5));
    // Across a year.
    c = cycle_for(midnight(2026, 1, 2), 15);
    CHECK(c.start == midnight(2025, 12, 15));
    CHECK(c.end == midnight(2026, 1, 15));
    // Exactly at the renewal instant: the new cycle.
    c = cycle_for(midnight(2026, 6, 1), 1);
    CHECK(c.start == midnight(2026, 6, 1));
    // Out-of-range days are clamped, not trusted.
    CHECK(cycle_for(midnight(2026, 6, 10), 0).start == midnight(2026, 6, 1));
    CHECK(cycle_for(midnight(2026, 6, 30), 31).start == midnight(2026, 6, 28));

    // Projection: nothing extrapolated from the first 1% of a cycle.
    const Cycle m{0, 1000};
    CHECK(project_to_cycle_end(500, m, 5) == 500);
    CHECK(project_to_cycle_end(500, m, 500) == 1000);
    CHECK(project_to_cycle_end(500, m, 2000) == 500);   // past the end: as measured
    CHECK(cycle_elapsed_fraction(Cycle{10, 10}, 10) == 1.0);
}

// The cap's usage: closed days from the daily tier, the rest from minutes.
// On the day after a 25-hour day, the boundary between them must be the real
// next midnight, or the extra hour is counted twice.
void quota_across_a_long_day(const QString& path) {
    {
        Store s(path.toStdString());
        s.migrate();
        s.upsert_iface(2, "wlan0", "wifi");
        s.set_config("cycle_start_day", "1");
    }
    // Europe/London leaves BST on 25 Oct 2026: that local day has 25 hours.
    const std::int64_t day25 = midnight(2026, 10, 25);
    const std::int64_t day26 = midnight(2026, 10, 26);
    CHECK(day26 - day25 == 25 * 3600);
    CHECK(bucket_start(day25 + 12 * 3600, kRes1d) == day25);

    sqlite3* db = nullptr;
    sqlite3_open(path.toUtf8().constData(), &db);
    auto row = [&](int res, std::int64_t ts, int rx) {
        const std::string sql = "INSERT INTO iface_samples VALUES(" + std::to_string(res) +
                                "," + std::to_string(ts) + ",2," + std::to_string(rx) + ",0)";
        CHECK(sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK);
    };
    // The 25th as a daily row, including its last hour's minute row.
    row(86400, day25, 5000);
    row(60, day26 - 1800, 1000);   // 23:30 on the 25th: inside the daily row
    row(60, day26 + 600, 7);       // the 26th, not closed yet
    sqlite3_close(db);

    HistoryStore h;
    h.open(path);
    QuotaInfo got;
    QObject::connect(&h, &HistoryStore::quota, [&](const QuotaInfo& q) { got = q; });
    h.queryQuota(day26 + 12 * 3600);
    CHECK(got.used == 5007);
}

void frames() {
    // Two frames and a partial third in one read.
    QByteArray buf;
    auto add = [&](const QByteArray& body) {
        const quint32 n = quint32(body.size());
        buf.append(char(n & 0xFF)).append(char((n >> 8) & 0xFF));
        buf.append(char((n >> 16) & 0xFF)).append(char((n >> 24) & 0xFF));
        buf.append(body);
    };
    add(R"({"t":"hello","ver":1,"db":"/var/lib/bandsight/bandsight.db","ifaces":[{"id":2,"n":"eth0","kind":"physical"}]})");
    add(R"({"t":"sample","ts":1700000000,"ifaces":[{"id":2,"rx":10,"tx":20}],"apps":[]})");
    buf.append("\x05\x00", 2);   // the next frame's length, cut short
    auto out = extract_frames(buf);
    CHECK(out && out->size() == 2);
    CHECK(buf.size() == 2);      // the partial tail stays for the next read
    if (out && out->size() == 2) {
        const auto h = decode_hello((*out)[0]);
        CHECK(h && h->db_path == QLatin1String("/var/lib/bandsight/bandsight.db"));
        CHECK(h && h->ifaces.size() == 1 && h->ifaces[0].kind == QLatin1String("physical"));
        const auto s = decode_sample((*out)[1]);
        CHECK(s && s->ts == 1700000000 && s->ifaces.size() == 1 && s->ifaces[0].tx == 20);
        CHECK(!decode_hello((*out)[1]));   // the wrong type is refused
        CHECK(!decode_sample((*out)[0]));
        // A hello from a daemon that predates the version field.
        CHECK(h && h->version.isEmpty());
    }

    // The optional version field, as the daemon writes it and the GUI reads it.
    {
        const std::vector<HelloIface> ifs{{2, "eth0", "physical"}};
        const std::string with = encode_hello("/var/lib/bandsight/bandsight.db", ifs, "0.4.0");
        const std::string without = encode_hello("/var/lib/bandsight/bandsight.db", ifs);
        CHECK(with.find("\"version\":\"0.4.0\"") != std::string::npos);
        CHECK(without.find("version") == std::string::npos);   // absent, not ""
        CHECK(with.find("\"ver\":1") != std::string::npos);    // protocol unchanged
        const auto hv = decode_hello(QByteArray::fromStdString(with));
        CHECK(hv && hv->version == QLatin1String("0.4.0"));
        CHECK(hv && hv->db_path == QLatin1String("/var/lib/bandsight/bandsight.db"));
        CHECK(hv && hv->ifaces.size() == 1 && hv->ifaces[0].name == QLatin1String("eth0"));
        const auto hn = decode_hello(QByteArray::fromStdString(without));
        CHECK(hn && hn->version.isEmpty() && hn->ifaces.size() == 1);
        // Escaped like every other string in the frame.
        const auto he = decode_hello(QByteArray::fromStdString(
            encode_hello("/db", {}, "1.0 \"rc\"\\x")));
        CHECK(he && he->version == QLatin1String("1.0 \"rc\"\\x"));
        // A newer daemon's extra keys are ignored, and a version that is not a
        // string reads as absent rather than refusing the frame.
        const auto hx = decode_hello(R"({"t":"hello","ver":2,"version":7,"db":"/d",)"
                                     R"("future":{"a":[1,2]},"ifaces":[]})");
        CHECK(hx && hx->db_path == QLatin1String("/d") && hx->version.isEmpty());
    }

    // An implausible length is corruption, not an allocation.
    QByteArray bad("\xff\xff\xff\x7f", 4);
    CHECK(!extract_frames(bad));
    QByteArray limit;
    const quint32 over = kMaxFrameBytes + 1;
    limit.append(char(over & 0xFF)).append(char((over >> 8) & 0xFF));
    limit.append(char((over >> 16) & 0xFF)).append(char((over >> 24) & 0xFF));
    CHECK(!extract_frames(limit));
    QByteArray empty;
    CHECK(extract_frames(empty) && extract_frames(empty)->empty());

    // Garbage bodies decode to nothing rather than to zeros.
    CHECK(!decode_hello("not json"));
    CHECK(!decode_sample(R"({"t":"sample"})"));
    CHECK(decode_error(R"({"t":"error","msg":"cap_bytes must be a non-negative integer"})")
              .startsWith(QLatin1String("cap_bytes")));
}

void zones() {
    for (const char* lan : {"10.1.2.3", "172.16.0.1", "172.31.255.255", "192.168.1.1",
                            "169.254.10.10", "224.0.0.251", "fe80::1", "fd00::1", "ff02::fb"})
        CHECK(host_zone(QString::fromLatin1(lan)) == HostZone::Lan);
    for (const char* wan : {"1.1.1.1", "172.32.0.1", "8.8.8.8", "2606:4700::1111"})
        CHECK(host_zone(QString::fromLatin1(wan)) == HostZone::Wan);
    CHECK(host_zone(QString()) == HostZone::None);
    CHECK(host_zone(QStringLiteral("not an address")) == HostZone::None);
}

}  // namespace

int main(int argc, char** argv) {
    // Fixed so DST cases are the same everywhere; skipped without tzdata.
    const bool have_tz = std::filesystem::exists("/usr/share/zoneinfo/Europe/London");
    ::setenv("TZ", have_tz ? "Europe/London" : "UTC", 1);
    ::tzset();
    QCoreApplication app(argc, argv);

    cycles();
    frames();
    zones();
    if (have_tz) {
        QTemporaryDir dir;
        CHECK(dir.isValid());
        quota_across_a_long_day(dir.filePath(QStringLiteral("q.db")));
    } else {
        std::printf("test_gui_logic: no tzdata, DST quota case skipped\n");
    }

    if (g_failures) std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    else std::printf("test_gui_logic: all checks passed\n");
    return g_failures ? 1 : 0;
}
