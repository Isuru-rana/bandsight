// SPDX-License-Identifier: GPL-3.0-or-later
// core::HistoryDb (no Qt) against a database the daemon's own Store wrote, the
// open-failure kinds a command-line user needs told apart, and the Qt-free
// live-frame decoder.
#include <grp.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>

#include "check.h"
#include "core/frames.h"
#include "core/history_db.h"
#include "fixture_db.h"
#include "json_frame.h"

using namespace bandsight;
using bandsight::fixture::kFixtureNow;

namespace {

std::uint64_t app_sum(const std::vector<core::AppTotal>& v, const std::string& exe, bool rx) {
    for (const auto& a : v)
        if (a.exe == exe) return rx ? a.rx : a.tx;
    return 0;
}

void queries(const std::string& path) {
    core::HistoryDb db;
    CHECK(db.open(path));

    // Last 24 hours at the minute tier: the window the CLI defaults to.
    const core::Span day = core::window_span(60, 86400, kFixtureNow);
    const auto apps = db.app_totals(day.from, day.to, 60);
    CHECK(apps.has_value());
    if (apps) {
        // Largest first; loopback-only python is not there; tailscaled's
        // route-less bytes (row 0) are.
        CHECK(!apps->empty() && apps->front().exe == "/usr/lib/firefox/firefox");
        CHECK(app_sum(*apps, "/usr/bin/python3.12", true) == 0);
        CHECK(app_sum(*apps, "/usr/sbin/tailscaled", true) == 8'400'000);
        // 5 h + 3 min + 40 s ago; the 40 s row is in a closed minute.
        CHECK(app_sum(*apps, "/usr/lib/firefox/firefox", true) ==
              640'000'000ull + 12'000'000 + 4'500'000);
        for (std::size_t i = 1; i < apps->size(); ++i)
            CHECK((*apps)[i - 1].rx + (*apps)[i - 1].tx >= (*apps)[i].rx + (*apps)[i].tx);
    }

    const auto ifs = db.iface_totals(day.from, day.to, 60);
    CHECK(ifs.has_value());
    if (ifs) {
        // Every named interface, alphabetical, idle ones at zero; other-netns is
        // the row the daemon keeps for container sockets.
        std::vector<std::string> names;
        for (const auto& i : *ifs) names.push_back(i.name);
        CHECK((names == std::vector<std::string>{"docker0", "eth0", "lo", "other-netns", "tailscale0", "wlan0"}));
        for (const auto& i : *ifs)
            if (i.name == "lo") CHECK(i.rx == 0 && i.kind == "loopback" && i.ids == std::vector<int>{1});
    }

    const auto hosts = db.host_pairs_window(60, 86400, kFixtureNow);
    CHECK(hosts.collected && hosts.ok && hosts.since == 0);
    bool saw_no_peer = false, saw_loopback = false;
    for (const auto& p : hosts.pairs) {
        if (p.ip.empty()) saw_no_peer = true;
        if (p.ip == "127.0.0.1") saw_loopback = true;
        if (p.ip == "2606:4700::1111") CHECK(p.hostname == "one.one.one.one");
    }
    CHECK(saw_no_peer && !saw_loopback);

    // Types recorded since the first write, 20 days ago: inside a 30-day
    // window, so the 30-day answer says so; outside 24 hours, so that one does not.
    const auto types = db.type_totals_window(60, 86400, kFixtureNow);
    CHECK(types.collected && types.ok && types.since == 0 && !types.totals.empty());
    const auto month = db.type_totals_window(3600, 30 * 86400, kFixtureNow);
    CHECK(month.since == kFixtureNow - 20 * 86400);

    // The cap: the cycle that began on 1 November, closed days from the daily
    // tier and the rest from the minute tier - everything measured since then
    // except the current minute, which is not rolled up yet.
    const core::Quota q = db.quota(kFixtureNow);
    CHECK(q.cap == 100'000'000'000ull && q.cycle_day == 1);
    const std::int64_t nov1 = 1'793'491'200;   // 2026-11-01 00:00 UTC
    CHECK(q.cycle_start == nov1);
    CHECK(q.cycle_end == 1'796'083'200);       // 2026-12-01 00:00 UTC
    std::uint64_t expect = 0;
    for (const auto& e : fixture::events()) {
        const std::int64_t ts = kFixtureNow - e.ago;
        if (ts < nov1 || ts >= kFixtureNow - 30) continue;   // 30 s: the current minute
        if (e.iface == 2 || e.iface == 3 || e.iface == 0) expect += e.rx + e.tx;
    }
    for (const auto& [ago, rx] : fixture::unowned_rx())
        if (kFixtureNow - ago >= nov1) expect += rx + rx / 20;
    CHECK(q.used == expect);
    CHECK(q.projected > q.used);

    // Series: one point per bucket, zero-filled.
    bool ok = false;
    const core::Span hour = core::window_span(60, 3600, kFixtureNow);
    const auto pts = db.series(60, hour.from, hour.to, {2, 3}, &ok);
    CHECK(ok && pts.size() == 60);
    std::uint64_t rx = 0;
    for (const auto& p : pts) rx += p.rx;
    CHECK(rx == 84'000'000ull + 9'000 + 26'000'000 + 3'300'000 + 41'000'000 + 12'220'000 +
                    4'500'000);

    CHECK(db.config_value("cap_bytes") == std::string("100000000000"));
    CHECK(!db.config_value("no such key"));
}

void open_failures(const std::string& dir) {
    core::HistoryDb db;
    CHECK(!db.open(dir + "/missing.db"));
    CHECK(db.open_error() == core::OpenError::NotFound);
    CHECK(!db.is_open());

    const std::string junk = dir + "/junk.db";
    std::ofstream(junk) << "this is not a database, it is a text file long enough to have a header";
    CHECK(!db.open(junk));
    CHECK(db.open_error() == core::OpenError::NotADatabase);
    // Without the read check SQLite opens it, as the Qt driver always did.
    CHECK(db.open(junk, core::HistoryDb::Check::None));
    db.close();

    // Unreadable: run as an unprivileged user when the test itself is root,
    // since root reads anything.
    const std::string locked = dir + "/locked.db";
    fixture::write(locked);
    ::chmod(locked.c_str(), 0000);   // 0600 is still readable by its owner
    const pid_t pid = ::fork();
    if (pid == 0) {
        if (::geteuid() == 0 &&
            (::setgroups(0, nullptr) != 0 || ::setgid(65534) != 0 || ::setuid(65534) != 0))
            ::_exit(3);
        if (::geteuid() == 0) ::_exit(3);
        core::HistoryDb d;
        const bool opened = d.open(locked);
        ::_exit(!opened && d.open_error() == core::OpenError::Permission ? 0 : 1);
    }
    int status = 0;
    ::waitpid(pid, &status, 0);
    // Exit 3: no user to drop to, which is not this code's failure.
    CHECK(WIFEXITED(status) && (WEXITSTATUS(status) == 0 || WEXITSTATUS(status) == 3));
}

void frames() {
    std::string buf = frame(encode_hello("/var/lib/bandsight/bandsight.db",
                                         {{2, "eth0", "physical"}}, "0.5.0")) +
                      frame(encode_sample(1700000000, {{2, 1500, 300}}, {}));
    const std::string whole = buf;
    buf.resize(buf.size() - 3);   // a partial tail stays for the next read
    std::vector<std::string> out;
    CHECK(core::extract_frames(buf, out));
    CHECK(out.size() == 1 && buf.size() == whole.size() - 3 - out[0].size() - 4);
    const auto h = core::decode_hello(out[0]);
    CHECK(h && h->db_path == "/var/lib/bandsight/bandsight.db" && h->version == "0.5.0");
    CHECK(h && h->ifaces.size() == 1 && h->ifaces[0].name == "eth0" && h->ifaces[0].kind == "physical");
    CHECK(!core::decode_sample(out[0]));

    buf += whole.substr(whole.size() - 3);
    out.clear();
    CHECK(core::extract_frames(buf, out) && out.size() == 1 && buf.empty());
    const auto s = core::decode_sample(out[0]);
    CHECK(s && s->ts == 1700000000 && s->ifaces.size() == 1 && s->ifaces[0].rx == 1500 &&
          s->ifaces[0].tx == 300);
    CHECK(core::frame_type(out[0]) == "sample");

    // An old daemon's hello has no version; a non-string one is absent.
    CHECK(core::decode_hello(encode_hello("/d", {}))->version.empty());
    CHECK(core::decode_hello(R"({"t":"hello","version":7,"db":"/d","ifaces":[]})")->version.empty());
    CHECK(!core::decode_hello(R"({"t":"hello","ifaces":[]})"));
    CHECK(!core::decode_sample("not json"));

    std::string huge("\xff\xff\xff\x7f", 4);
    CHECK(!core::extract_frames(huge, out));
}

void parsing() {
    CHECK(core::parse_u64("100000000000") == 100'000'000'000ull);
    CHECK(core::parse_u64(" 7 ") == 7u);
    CHECK(!core::parse_u64("-1") && !core::parse_u64("") && !core::parse_u64("1e9") &&
          !core::parse_u64("99999999999999999999"));
    CHECK(core::parse_i64("-5") == -5 && !core::parse_i64("5x"));
}

}  // namespace

int main() {
    ::setenv("TZ", "UTC", 1);
    ::tzset();
    const std::string dir = std::filesystem::temp_directory_path() /
                            ("bandsight-history-db-" + std::to_string(::getpid()));
    std::filesystem::create_directories(dir);
    ::chmod(dir.c_str(), 0755);   // the unprivileged child must reach locked.db
    const std::string path = dir + "/fixture.db";
    fixture::write(path);

    queries(path);
    open_failures(dir);
    frames();
    parsing();

    std::filesystem::remove_all(dir);
    return g_failures == 0 ? 0 : 1;
}
