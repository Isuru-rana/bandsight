// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/host_zone.h"

#include <arpa/inet.h>
#include <netinet/in.h>

#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>

namespace bandsight::core {

namespace {

bool in_prefix(const unsigned char* addr, const unsigned char* net, int bits) {
    for (int i = 0; bits > 0; ++i, bits -= 8) {
        const unsigned mask = bits >= 8 ? 0xffu : (0xffu << (8 - bits)) & 0xffu;
        if ((addr[i] & mask) != (net[i] & mask)) return false;
    }
    return true;
}

}  // namespace

HostZone host_zone(std::string_view ip) {
    if (ip.empty()) return HostZone::None;
    // Parsed as QHostAddress parsed it when this rule lived in the GUI, so the
    // move changed no answer (tests/test_query_parity.cpp): surrounding
    // whitespace is ignored, IPv4 takes the inet_aton forms ("10.1", "0x0a.0.0.1",
    // octal "010.0.0.1" = 8.0.0.1), and an IPv6 scope ("fe80::1%eth0") is not
    // part of the address. The daemon itself only ever stores canonical text.
    std::size_t a = 0, b = ip.size();
    while (a < b && std::isspace(static_cast<unsigned char>(ip[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(ip[b - 1]))) --b;
    std::string text(ip.substr(a, b - a));
    for (char c : text)
        if (std::isspace(static_cast<unsigned char>(c))) return HostZone::None;
    if (text.empty()) return HostZone::None;
    if (const auto pct = text.find('%'); pct != std::string::npos && text.find(':') != std::string::npos)
        text.resize(pct);

    in_addr v4{};
    if (text.find(':') == std::string::npos && inet_aton(text.c_str(), &v4) != 0) {
        unsigned char a4[4];
        std::memcpy(a4, &v4.s_addr, 4);   // network order: a4[0] is the first octet
        static const struct { unsigned char net[4]; int bits; } kLan4[] = {
            {{10, 0, 0, 0}, 8},     {{172, 16, 0, 0}, 12}, {{192, 168, 0, 0}, 16},
            {{169, 254, 0, 0}, 16}, {{224, 0, 0, 0}, 4},
        };
        for (const auto& n : kLan4)
            if (in_prefix(a4, n.net, n.bits)) return HostZone::Lan;
        return HostZone::Wan;
    }
    unsigned char a6[16];
    if (inet_pton(AF_INET6, text.c_str(), a6) == 1) {
        static const struct { unsigned char net[16]; int bits; } kLan6[] = {
            {{0xfe, 0x80}, 10}, {{0xfc, 0x00}, 7}, {{0xff, 0x00}, 8},
        };
        for (const auto& n : kLan6)
            if (in_prefix(a6, n.net, n.bits)) return HostZone::Lan;
        return HostZone::Wan;
    }
    return HostZone::None;
}

}  // namespace bandsight::core
