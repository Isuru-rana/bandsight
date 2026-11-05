// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/service_names.h"

namespace bandsight::core {

std::string port_label(int proto, int port) {
    return std::to_string(port) + (proto == 17 ? "/udp" : "/tcp");
}

// The daemon prefers the ports named here when it picks a socket's service
// side (is_known_service_port in daemon/traffic_type.cpp); the two lists are
// kept in step, and a test checks it.
const char* known_service_name(int proto, int port) {
    const bool udp = proto == 17;
    switch (port) {
    case 0: return "Other";
    case 443: return udp ? "QUIC" : "HTTPS";
    case 80: return "HTTP";
    case 53: return "DNS";
    case 853: return udp ? "DNS-over-QUIC" : "DNS-over-TLS";
    case 22: return "SSH";
    case 123: return "NTP";
    case 25: case 465: case 587: case 143: case 993: case 995: return "Mail";
    case 1883: case 8883: return "MQTT";
    case 3478: return "WebRTC/STUN";
    default: break;
    }
    if (udp) {
        if (port == 5353) return "mDNS";
        if (port == 41641) return "Tailscale";
        if (port == 51820) return "WireGuard";
        if (port >= 19302 && port <= 19309) return "WebRTC/STUN";
    }
    return nullptr;
}

std::string service_name(int proto, int port) {
    if (const char* n = known_service_name(proto, port)) return n;
    return port_label(proto, port);
}

}  // namespace bandsight::core
