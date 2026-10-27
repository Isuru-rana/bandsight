// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "collector.h"
#include "store.h"

namespace bandsight {

// The kernel stores every peer as 16 bytes, IPv4 as v4-mapped, so one path
// handles both families. Formats as people write addresses: IPv4 dotted (never
// ::ffff:...), IPv6 compressed, and no address - all-zero, or ::ffff:0.0.0.0
// for an unconnected IPv4 socket, which has no peer - as "".
std::string format_peer(const std::uint8_t raddr[16]);

// One tick of drained sockets -> one delta per (app, interface, peer).
// app_of resolves a socket's process to an app id (nullopt: skip it, as the app
// path does when an id cannot be had); iface_of clamps an ifindex the daemon has
// not seen in its own netns to the sentinel 0. Sorted for determinism.
std::vector<HostDelta> aggregate_hosts(
    const std::vector<std::pair<SockKey, SockVal>>& entries,
    const std::function<std::optional<std::int64_t>(const SockVal&)>& app_of,
    const std::function<int(std::uint32_t)>& iface_of);

}  // namespace bandsight
