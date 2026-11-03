// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "collector.h"
#include "store.h"

namespace bandsight {

// The local port range the kernel hands out to connect() and autobind
// (net.ipv4.ip_local_port_range). A local port inside it means this end
// picked its port, i.e. the socket initiated the conversation.
struct EphemeralRange {
    std::uint16_t lo = 32768;
    std::uint16_t hi = 60999;
    bool contains(std::uint16_t p) const { return p >= lo && p <= hi; }
};

// Reads ip_local_port_range from the given procfs file; the kernel default
// if it cannot be read or parsed.
EphemeralRange read_ephemeral_range(
    const std::string& path = "/proc/sys/net/ipv4/ip_local_port_range");

// A port that names a service on its own: below 1024, or one of the services
// listed in traffic_type.cpp (the same ports the GUI names).
bool is_known_service_port(std::uint8_t proto, std::uint16_t port);

// Which of a socket's two ports identifies the service, 0 for "other":
//   - exactly one side is a known service port: that side;
//   - both known: the remote one if this end initiated (local port in the
//     ephemeral range), otherwise the local one;
//   - neither known: the local port, only if this machine is the listening
//     side (local port outside the ephemeral range and below 49152);
//     otherwise 0. An unknown remote port is never stored: peers pick them
//     at random, and each one would be a row of its own.
// An unconnected UDP socket has rport 0 and therefore usually lands on 0.
std::uint16_t service_port(std::uint8_t proto, std::uint16_t lport, std::uint16_t rport,
                           const EphemeralRange& eph);

// One tick of drained sockets -> one delta per (interface, proto, port).
// iface_of is the same ifindex clamp the app and host paths use. Sorted.
std::vector<TypeDelta> aggregate_types(
    const std::vector<std::pair<SockKey, SockVal>>& entries, const EphemeralRange& eph,
    const std::function<int(std::uint32_t)>& iface_of);

}  // namespace bandsight
