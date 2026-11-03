// SPDX-License-Identifier: GPL-3.0-or-later
#include "traffic_type.h"

#include <fstream>
#include <map>
#include <tuple>

namespace bandsight {

EphemeralRange read_ephemeral_range(const std::string& path) {
    EphemeralRange r;
    std::ifstream in(path);
    unsigned lo = 0, hi = 0;
    if (in >> lo >> hi && lo > 0 && lo <= hi && hi <= 65535) {
        r.lo = static_cast<std::uint16_t>(lo);
        r.hi = static_cast<std::uint16_t>(hi);
    }
    return r;
}

bool is_known_service_port(std::uint8_t proto, std::uint16_t port) {
    if (port == 0) return false;
    if (port < 1024) return true;
    const bool udp = proto == 17;
    switch (port) {
    case 1883: case 8883:          // MQTT
    case 3478:                     // STUN / TURN
        return true;
    case 5353:                     // mDNS
    case 41641:                    // Tailscale
    case 51820:                    // WireGuard
        return udp;
    default:
        return udp && port >= 19302 && port <= 19309;   // Google STUN
    }
}

std::uint16_t service_port(std::uint8_t proto, std::uint16_t lport, std::uint16_t rport,
                           const EphemeralRange& eph) {
    const bool kl = is_known_service_port(proto, lport);
    const bool kr = is_known_service_port(proto, rport);
    if (kl != kr) return kl ? lport : rport;
    if (kl) return eph.contains(lport) ? rport : lport;   // both known

    // Neither is known. Only a port this machine listens on is kept: there are
    // few of those (a dev server, a database). A remote port is whatever a
    // peer happened to pick - P2P and WebRTC peers use random ports anywhere
    // in 1024-65535 - and keeping it would add a row per peer, forever in the
    // daily tier.
    if (eph.contains(lport)) return 0;   // this end connected: remote port
    if (lport >= 49152) return 0;        // dynamic range, also never a service
    return lport;
}

std::vector<TypeDelta> aggregate_types(
    const std::vector<std::pair<SockKey, SockVal>>& entries, const EphemeralRange& eph,
    const std::function<int(std::uint32_t)>& iface_of) {
    std::map<std::tuple<int, int, int>, TypeDelta> acc;
    for (const auto& entry : entries) {
        const SockVal& v = entry.second;
        if (v.rx == 0 && v.tx == 0) continue;
        const int ifx = iface_of(v.ifindex);
        const int port = service_port(v.proto, v.lport, v.rport, eph);
        TypeDelta& d = acc[{ifx, v.proto, port}];
        d.ifindex = ifx;
        d.proto = v.proto;
        d.port = port;
        d.rx += v.rx;
        d.tx += v.tx;
    }
    std::vector<TypeDelta> out;
    out.reserve(acc.size());
    for (auto& kv : acc) out.push_back(kv.second);
    return out;
}

}  // namespace bandsight
