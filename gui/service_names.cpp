// SPDX-License-Identifier: GPL-3.0-or-later
#include "service_names.h"

#include <QCoreApplication>

namespace bandsight::gui {

namespace {
QString tr(const char* s) { return QCoreApplication::translate("Services", s); }
}  // namespace

QString port_label(int proto, int port) {
    return QStringLiteral("%1/%2").arg(port).arg(
        proto == 17 ? QStringLiteral("udp") : QStringLiteral("tcp"));
}

// The daemon prefers the ports named here when it picks a socket's service
// side (is_known_service_port in daemon/traffic_type.cpp); the two lists are
// kept in step, and a test checks it.
QString service_name(int proto, int port) {
    const bool udp = proto == 17;
    switch (port) {
    case 0: return tr("Other");
    case 443: return udp ? tr("QUIC") : tr("HTTPS");
    case 80: return tr("HTTP");
    case 53: return tr("DNS");
    case 853: return udp ? tr("DNS-over-QUIC") : tr("DNS-over-TLS");
    case 22: return tr("SSH");
    case 123: return tr("NTP");
    case 25: case 465: case 587: case 143: case 993: case 995: return tr("Mail");
    case 1883: case 8883: return tr("MQTT");
    case 3478: return tr("WebRTC/STUN");
    default: break;
    }
    if (udp) {
        if (port == 5353) return tr("mDNS");
        if (port == 41641) return tr("Tailscale");
        if (port == 51820) return tr("WireGuard");
        if (port >= 19302 && port <= 19309) return tr("WebRTC/STUN");
    }
    return port_label(proto, port);
}

}  // namespace bandsight::gui
