// SPDX-License-Identifier: GPL-3.0-or-later
// Traffic types in the daemon: the service-port rule, per-tick aggregation,
// the schema 3 -> 4 migration, and type_samples in rollup and retention.
#include <sqlite3.h>

#include <cstdio>
#include <string>

#include "check.h"
#include "rollup.h"
#include "store.h"
#include "time_buckets.h"
#include "traffic_type.h"

using namespace bandsight;

namespace {

constexpr std::uint8_t kTcp = 6, kUdp = 17;

std::int64_t scalar(sqlite3* db, const std::string& sql) {
    sqlite3_stmt* st = nullptr;
    std::int64_t v = -1;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &st, nullptr) == SQLITE_OK &&
        sqlite3_step(st) == SQLITE_ROW)
        v = sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    return v;
}

void service_port_rule() {
    const EphemeralRange eph;   // kernel default 32768-60999

    // Exactly one side well known: that side, whoever connected.
    CHECK(service_port(kTcp, 51000, 443, eph) == 443);     // client to HTTPS
    CHECK(service_port(kTcp, 443, 51000, eph) == 443);     // HTTPS server
    CHECK(service_port(kTcp, 22, 40000, eph) == 22);       // SSH server
    CHECK(service_port(kUdp, 41000, 3478, eph) == 3478);   // STUN, registered port
    CHECK(service_port(kUdp, 41641, 0, eph) == 41641);     // Tailscale, unconnected
    CHECK(service_port(kUdp, 51820, 0, eph) == 51820);     // WireGuard listener
    CHECK(service_port(kUdp, 5353, 0, eph) == 5353);       // mDNS
    CHECK(service_port(kUdp, 50000, 19305, eph) == 19305); // Google STUN

    // Neither known: the remote port if this end connected, else the local one.
    CHECK(service_port(kTcp, 45000, 8080, eph) == 8080);   // client to a dev server
    CHECK(service_port(kTcp, 8080, 45000, eph) == 8080);   // the dev server
    CHECK(service_port(kTcp, 5432, 33000, eph) == 5432);   // postgres server

    // Both in the dynamic range: other.
    CHECK(service_port(kUdp, 50000, 60000, eph) == 0);
    CHECK(service_port(kTcp, 61000, 62000, eph) == 0);
    // An unknown dynamic-range port is never stored as itself.
    CHECK(service_port(kTcp, 20000, 55555, eph) == 20000);   // server on 20000
    CHECK(service_port(kTcp, 33000, 55555, eph) == 0);       // client, dynamic remote
    CHECK(service_port(kTcp, 50000, 20000, eph) == 20000);   // registered port kept
    // Unconnected UDP from an ephemeral port: no remote port, other.
    CHECK(service_port(kUdp, 45000, 0, eph) == 0);

    // A protocol-specific port does not count for the other protocol.
    CHECK(is_known_service_port(kUdp, 51820));
    CHECK(!is_known_service_port(kTcp, 51820));
    CHECK(!is_known_service_port(kTcp, 0));
    CHECK(is_known_service_port(kTcp, 1023));
    CHECK(!is_known_service_port(kTcp, 1024));

    // A narrowed ephemeral range changes who is the client.
    EphemeralRange narrow;
    narrow.lo = 50000;
    narrow.hi = 50100;
    CHECK(service_port(kTcp, 45000, 8080, narrow) == 45000);
}

void ephemeral_range_file() {
    const char* path = "test_ephemeral_range.txt";
    if (FILE* f = std::fopen(path, "w")) {
        std::fputs("10000\t20000\n", f);
        std::fclose(f);
    }
    const EphemeralRange r = read_ephemeral_range(path);
    CHECK(r.lo == 10000 && r.hi == 20000);
    std::remove(path);

    const EphemeralRange d = read_ephemeral_range("/nonexistent/ip_local_port_range");
    CHECK(d.lo == 32768 && d.hi == 60999);
}

void aggregation() {
    const EphemeralRange eph;
    std::vector<std::pair<SockKey, SockVal>> socks;
    auto add = [&](std::uint8_t proto, std::uint16_t lport, std::uint16_t rport,
                   std::uint32_t ifx, std::uint64_t rx, std::uint64_t tx) {
        SockVal v;
        v.proto = proto;
        v.lport = lport;
        v.rport = rport;
        v.ifindex = ifx;
        v.rx = rx;
        v.tx = tx;
        socks.push_back({SockKey{socks.size() + 1}, v});
    };
    add(kTcp, 50001, 443, 2, 100, 10);
    add(kTcp, 50002, 443, 2, 200, 20);   // same service, same interface: merged
    add(kUdp, 50003, 443, 2, 1000, 5);   // QUIC: a different row from HTTPS
    add(kTcp, 50004, 443, 9, 1, 1);      // unknown interface -> clamped to 0
    add(kUdp, 50005, 60000, 2, 7, 7);    // other
    add(kTcp, 50006, 443, 2, 0, 0);      // nothing moved: no row

    const auto out = aggregate_types(socks, eph,
                                     [](std::uint32_t i) { return i == 2 ? 2 : 0; });
    CHECK(out.size() == 4);
    bool https = false, quic = false, unknown_if = false, other = false;
    for (const auto& d : out) {
        if (d.ifindex == 2 && d.proto == kTcp && d.port == 443)
            https = d.rx == 300 && d.tx == 30;
        if (d.ifindex == 2 && d.proto == kUdp && d.port == 443)
            quic = d.rx == 1000 && d.tx == 5;
        if (d.ifindex == 0 && d.proto == kTcp && d.port == 443)
            unknown_if = d.rx == 1 && d.tx == 1;
        if (d.ifindex == 2 && d.proto == kUdp && d.port == 0)
            other = d.rx == 7 && d.tx == 7;
    }
    CHECK(https && quic && unknown_if && other);
}

// A v3 database is a v4 one without type_samples and with schema_version 3.
void migration_3_to_4() {
    const char* path = "test_migration_v3.db";
    std::remove(path);
    std::int64_t app = 0;
    {
        Store s(path);
        s.migrate();
        s.upsert_iface(2, "eth0", "physical");
        app = s.upsert_app("/usr/bin/curl", "curl", "");
        s.write_samples(1'700'000'000, {{2, 10, 20}}, {{app, 2, 10, 20}},
                        {{app, 2, "1.1.1.1", 10, 20}});
        sqlite3_exec(s.handle(), "DROP TABLE type_samples;"
                                 "UPDATE config SET value='3' WHERE key='schema_version'",
                     nullptr, nullptr, nullptr);
        CHECK(s.schema_version() == 3);
    }
    {
        Store s(path);
        bool ok = true;
        try { s.migrate(); } catch (...) { ok = false; }
        CHECK(ok);
        CHECK(s.schema_version() == 4);
        CHECK(scalar(s.handle(), "SELECT COUNT(*) FROM sqlite_master WHERE name='type_samples'")
              == 1);
        // Existing history is untouched.
        CHECK(scalar(s.handle(), "SELECT SUM(rx) FROM app_samples") == 10);
        CHECK(scalar(s.handle(), "SELECT SUM(tx) FROM app_host_samples") == 20);
        // And the new table takes writes.
        bool wrote = true;
        try {
            s.write_samples(1'700'000'001, {}, {}, {}, {{2, kTcp, 443, 5, 6}});
        } catch (...) {
            wrote = false;
        }
        CHECK(wrote);
        // Idempotent on the next start.
        try { s.migrate(); } catch (...) { ok = false; }
        CHECK(ok && s.schema_version() == 4);
    }
    std::remove(path);
    std::remove((std::string(path) + "-wal").c_str());
    std::remove((std::string(path) + "-shm").c_str());
}

void rollup_and_retention() {
    Store s(":memory:");
    s.migrate();
    s.upsert_iface(2, "eth0", "physical");
    const std::int64_t t0 = bucket_start(1'700'000'000, kRes1h);
    // Two minutes of the same hour, and a repeat write in the same minute.
    s.write_samples(t0 + 5, {}, {}, {}, {{2, kTcp, 443, 100, 10}});
    s.write_samples(t0 + 6, {}, {}, {}, {{2, kTcp, 443, 1, 1}});
    s.write_samples(t0 + 125, {}, {}, {}, {{2, kTcp, 443, 50, 5}, {2, kUdp, 0, 9, 9}});
    sqlite3* db = s.handle();
    CHECK(scalar(db, "SELECT COUNT(*) FROM type_samples WHERE res=60") == 3);
    CHECK(scalar(db, "SELECT rx FROM type_samples WHERE res=60 AND ts=" +
                         std::to_string(t0) + " AND port=443") == 101);

    rollup_tier(db, kRes1m, kRes1h, t0 + 2 * 3600);
    CHECK(scalar(db, "SELECT rx FROM type_samples WHERE res=3600 AND ts=" +
                         std::to_string(t0) + " AND proto=6 AND port=443") == 151);
    CHECK(scalar(db, "SELECT tx FROM type_samples WHERE res=3600 AND proto=17 AND port=0")
          == 9);
    // Re-running replaces rather than doubles.
    rollup_tier(db, kRes1m, kRes1h, t0 + 2 * 3600);
    CHECK(scalar(db, "SELECT SUM(rx) FROM type_samples WHERE res=3600") == 160);

    // Minute rows go after two days, hourly after ninety, daily never.
    sqlite3_exec(db, ("INSERT INTO type_samples VALUES(86400," + std::to_string(t0) +
                      ",2,6,443,1,1)").c_str(), nullptr, nullptr, nullptr);
    enforce_retention(db, t0 + 3 * 86400);
    CHECK(scalar(db, "SELECT COUNT(*) FROM type_samples WHERE res=60") == 0);
    CHECK(scalar(db, "SELECT COUNT(*) FROM type_samples WHERE res=3600") == 2);
    enforce_retention(db, t0 + 91 * 86400);
    CHECK(scalar(db, "SELECT COUNT(*) FROM type_samples WHERE res=3600") == 0);
    CHECK(scalar(db, "SELECT COUNT(*) FROM type_samples WHERE res=86400") == 1);
}

}  // namespace

int main() {
    service_port_rule();
    ephemeral_range_file();
    aggregation();
    migration_3_to_4();
    rollup_and_retention();
    if (g_failures) std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    else std::printf("test_traffic_type: all checks passed\n");
    return g_failures ? 1 : 0;
}
