// SPDX-License-Identifier: GPL-3.0-or-later
// The Traffic type card's names for (protocol, port), and that every named
// port is one the daemon prefers when it picks a socket's service side.
#include <QString>

#include "check.h"
#include "service_names.h"
#include "traffic_type.h"

using bandsight::gui::port_label;
using bandsight::gui::service_name;

int main() {
    const int tcp = 6, udp = 17;
    auto is = [](const QString& got, const char* want) { return got == QLatin1String(want); };

    CHECK(is(service_name(tcp, 443), "HTTPS"));
    CHECK(is(service_name(udp, 443), "QUIC"));
    CHECK(is(service_name(tcp, 80), "HTTP"));
    CHECK(is(service_name(udp, 53), "DNS"));
    CHECK(is(service_name(tcp, 53), "DNS"));
    CHECK(is(service_name(tcp, 853), "DNS-over-TLS"));
    CHECK(is(service_name(tcp, 22), "SSH"));
    CHECK(is(service_name(udp, 41641), "Tailscale"));
    CHECK(is(service_name(udp, 51820), "WireGuard"));
    for (int p : {25, 465, 587, 993, 995, 143}) CHECK(is(service_name(tcp, p), "Mail"));
    CHECK(is(service_name(udp, 123), "NTP"));
    CHECK(is(service_name(udp, 5353), "mDNS"));
    CHECK(is(service_name(udp, 3478), "WebRTC/STUN"));
    for (int p = 19302; p <= 19309; ++p) CHECK(is(service_name(udp, p), "WebRTC/STUN"));
    CHECK(is(service_name(tcp, 1883), "MQTT"));
    CHECK(is(service_name(tcp, 8883), "MQTT"));
    CHECK(is(service_name(tcp, 0), "Other"));
    CHECK(is(service_name(udp, 0), "Other"));

    // UDP-only names do not leak onto TCP, and an unnamed port reads as itself.
    CHECK(is(service_name(tcp, 51820), "51820/tcp"));
    CHECK(is(service_name(tcp, 19302), "19302/tcp"));
    CHECK(is(service_name(tcp, 8080), "8080/tcp"));
    CHECK(is(port_label(udp, 443), "443/udp"));

    // Every port the card names is one the daemon keeps as itself, so a named
    // service is never folded into "Other" by the port choice.
    for (int proto : {tcp, udp})
        for (int port = 1; port <= 65535; ++port)
            if (service_name(proto, port) != port_label(proto, port))
                CHECK(bandsight::is_known_service_port(std::uint8_t(proto),
                                                       std::uint16_t(port)));

    if (g_failures) std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    else std::printf("test_service_names: all checks passed\n");
    return g_failures ? 1 : 0;
}
