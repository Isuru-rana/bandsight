// SPDX-License-Identifier: GPL-3.0-or-later
#include "host_accounting.h"

#include <arpa/inet.h>

#include <cstring>
#include <map>
#include <tuple>

namespace bandsight {

std::string format_peer(const std::uint8_t raddr[16]) {
    static const std::uint8_t kZero[16] = {};
    if (std::memcmp(raddr, kZero, 16) == 0) return {};

    static const std::uint8_t kMapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
    char buf[INET6_ADDRSTRLEN] = {};
    if (std::memcmp(raddr, kMapped, 12) == 0) {
        // The kernel writes the mapped prefix for every IPv4 socket, so an
        // unconnected one arrives as ::ffff:0.0.0.0 - no peer, same as all-zero.
        if (std::memcmp(raddr + 12, kZero, 4) == 0) return {};
        inet_ntop(AF_INET, raddr + 12, buf, sizeof(buf));
    } else {
        inet_ntop(AF_INET6, raddr, buf, sizeof(buf));
    }
    return buf;
}

std::vector<HostDelta> aggregate_hosts(
    const std::vector<std::pair<SockKey, SockVal>>& entries,
    const std::function<std::optional<std::int64_t>(const SockVal&)>& app_of,
    const std::function<int(std::uint32_t)>& iface_of) {
    std::map<std::tuple<std::int64_t, int, std::string>, HostDelta> acc;
    for (const auto& entry : entries) {
        const SockVal& val = entry.second;
        const auto app = app_of(val);
        if (!app) continue;
        const int ifx = iface_of(val.ifindex);
        std::string ip = format_peer(val.raddr);
        HostDelta& d = acc[{*app, ifx, ip}];
        d.app_id = *app;
        d.ifindex = ifx;
        d.ip = std::move(ip);
        d.rx += val.rx;
        d.tx += val.tx;
    }
    std::vector<HostDelta> out;
    out.reserve(acc.size());
    for (auto& kv : acc) out.push_back(std::move(kv.second));
    return out;
}

}  // namespace bandsight
