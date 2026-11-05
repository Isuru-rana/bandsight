// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// A history database written by the daemon's own Store and rolled up by its own
// rollup_tier, so every reader test runs against rows shaped exactly as a real
// daemon leaves them. Shared by the core, parity and command-line tests.
//
// kFixtureNow is 2026-11-05 12:00:30 UTC; tests that print times run with
// TZ=UTC. The data spans twenty days so every period has something in it:
//   - per-second rows in the last hour, minute rows for thirty days, hourly and
//     daily rows rolled up from them;
//   - two applications called "node" (labels disambiguated) and one that only
//     talks over loopback (never counted);
//   - LAN, WAN and no-peer hosts, named and unnamed;
//   - interface bytes no application owns (an Unattributed row);
//   - interfaces of every kind, measured or not.
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "rollup.h"
#include "store.h"

namespace bandsight::fixture {

constexpr std::int64_t kFixtureNow = 1'793'880'030;   // 2026-11-05 12:00:30 UTC

enum App {
    Firefox, Node, NodeNvm, Ssh, Curl, Python, Chrony, Apt, Spotify, Tailscaled, Git,
    Thunderbird, kAppCount
};

struct Event {
    std::int64_t ago;   // seconds before kFixtureNow
    int iface;          // 2 eth0, 3 wlan0, 1 lo, 5 tailscale0, 0 unknown
    App app;
    const char* ip;
    int proto;
    int port;
    std::uint64_t rx;
    std::uint64_t tx;
};

constexpr std::int64_t kMin = 60, kHour = 3600, kDay = 86400;

inline const std::vector<Event>& events() {
    static const std::vector<Event> e{
        // 20 days ago: outside the 7-day window and the billing cycle.
        {20 * kDay, 2, Firefox, "2606:4700::1111", 17, 443, 3'200'000'000, 41'000'000},
        {20 * kDay, 2, Apt, "91.189.91.81", 6, 80, 780'000'000, 2'100'000},
        // 6 days ago: inside 7 days, before the cycle (which starts 1 November).
        {6 * kDay, 3, Spotify, "35.186.224.25", 6, 443, 410'000'000, 9'000'000},
        {6 * kDay, 3, Firefox, "142.250.72.14", 6, 443, 1'250'000'000, 33'000'000},
        // 3 days ago and 30 hours ago: in the cycle, outside 24 hours.
        {3 * kDay, 2, Node, "104.16.0.35", 6, 443, 95'000'000, 1'500'000},
        {3 * kDay, 2, Git, "140.82.112.4", 6, 22, 61'000'000, 7'400'000},
        {30 * kHour, 2, Thunderbird, "203.0.113.5", 6, 993, 48'000'000, 3'900'000},
        // 5 hours ago.
        {5 * kHour, 2, Firefox, "2606:4700::1111", 17, 443, 640'000'000, 12'500'000},
        {5 * kHour, 2, NodeNvm, "104.16.0.35", 6, 443, 210'000'000, 4'200'000},
        {5 * kHour, 2, Ssh, "192.168.1.10", 6, 22, 15'000'000, 92'000'000},
        {5 * kHour, 0, Tailscaled, "", 17, 0, 8'400'000, 7'900'000},
        {5 * kHour, 1, Python, "127.0.0.1", 6, 8080, 900'000'000, 900'000'000},
        // 50 and 20 minutes ago: inside 1 hour, outside 30 minutes / inside it.
        {50 * kMin, 3, Spotify, "35.186.224.25", 6, 443, 84'000'000, 1'200'000},
        {50 * kMin, 3, Chrony, "162.159.200.1", 17, 123, 9'000, 9'000},
        {20 * kMin, 2, Curl, "151.101.1.69", 6, 443, 26'000'000, 310'000},
        {20 * kMin, 2, Node, "104.16.0.35", 6, 443, 3'300'000, 120'000},
        // The last five minutes, then the current minute (written live).
        {3 * kMin, 2, Firefox, "142.250.72.14", 6, 443, 12'000'000, 640'000},
        {3 * kMin, 2, Ssh, "192.168.1.10", 6, 22, 220'000, 1'800'000},
        {40, 2, Firefox, "2606:4700::1111", 17, 443, 4'500'000, 210'000},
        {10, 2, Curl, "151.101.1.69", 6, 443, 1'250'000, 25'000},
    };
    return e;
}

// Interface bytes no application owns - a virtual machine bridged onto eth0 -
// at these times, so the Unattributed row has something to show.
inline const std::map<std::int64_t, std::uint64_t>& unowned_rx() {
    static const std::map<std::int64_t, std::uint64_t> m{
        {6 * kDay, 520'000'000}, {5 * kHour, 380'000'000}, {20 * kMin, 41'000'000},
    };
    return m;
}

inline void write(const std::string& path, std::int64_t now = kFixtureNow) {
    Store s(path);
    s.migrate();
    s.upsert_iface(1, "lo", "loopback");
    s.upsert_iface(2, "eth0", "physical");
    s.upsert_iface(3, "wlan0", "wifi");
    s.upsert_iface(5, "tailscale0", "vpn");
    s.upsert_iface(6, "docker0", "virtual");
    s.set_config("cap_bytes", "100000000000");
    s.set_config("cycle_start_day", "1");

    const char* exes[kAppCount][2] = {
        {"/usr/lib/firefox/firefox", "Firefox"},
        {"/usr/bin/node", ""},
        {"/home/dev/.nvm/versions/node/v24.1.0/bin/node", ""},
        {"/usr/bin/ssh", ""},
        {"/usr/bin/curl", ""},
        {"/usr/bin/python3.12", ""},
        {"/usr/sbin/chronyd", ""},
        {"/usr/lib/apt/methods/http", "apt"},
        {"/opt/spotify/spotify", "Spotify"},
        {"/usr/sbin/tailscaled", ""},
        {"/usr/bin/git", ""},
        {"/usr/lib/thunderbird/thunderbird", "Thunderbird"},
    };
    std::int64_t ids[kAppCount];
    for (int a = 0; a < kAppCount; ++a) ids[a] = s.upsert_app(exes[a][0], exes[a][1], "");

    // Grouped by second, as one daemon tick writes them.
    std::map<std::int64_t, std::vector<const Event*>> by_ts;
    for (const Event& e : events()) by_ts[now - e.ago].push_back(&e);
    for (const auto& [ts, evs] : by_ts) {
        std::map<int, IfaceDelta> ifs;
        std::vector<AppDelta> apps;
        std::vector<HostDelta> hosts;
        std::vector<TypeDelta> types;
        for (const Event* e : evs) {
            // Only measured interfaces have interface counters (the daemon
            // skips the rest); route-less sockets are on eth0's counters.
            const int counted = e->iface == 0 ? 2 : e->iface;
            if (counted == 2 || counted == 3) {
                IfaceDelta& d = ifs[counted];
                d.ifindex = counted;
                d.rx += e->rx;
                d.tx += e->tx;
            }
            apps.push_back({ids[e->app], e->iface, e->rx, e->tx});
            hosts.push_back({ids[e->app], e->iface, e->ip, e->rx, e->tx});
            types.push_back({e->iface, e->proto, e->port, e->rx, e->tx});
        }
        if (const auto it = unowned_rx().find(now - ts); it != unowned_rx().end()) {
            IfaceDelta& d = ifs[2];
            d.ifindex = 2;
            d.rx += it->second;
            d.tx += it->second / 20;
        }
        std::vector<IfaceDelta> iv;
        for (const auto& [id, d] : ifs) iv.push_back(d);
        s.write_samples(ts, iv, apps, hosts, types);
    }
    s.set_hostnames({{"2606:4700::1111", "one.one.one.one"},
                     {"142.250.72.14", "www.google.com"},
                     {"104.16.0.35", "registry.npmjs.org"},
                     {"35.186.224.25", "spclient.wg.spotify.com"},
                     {"162.159.200.1", "time.cloudflare.com"},
                     {"140.82.112.4", "github.com"},
                     {"203.0.113.5", "imap.example.org"},
                     {"91.189.91.81", "archive.ubuntu.com"}});

    rollup_tier(s.handle(), 1, 60, now);
    rollup_tier(s.handle(), 60, 3600, now);
    rollup_tier(s.handle(), 3600, 86400, now);
}

}  // namespace bandsight::fixture
