// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string_view>

namespace bandsight::core {

// Where a remote host is, by its address alone. LAN: private IPv4 (10/8,
// 172.16/12, 192.168/16), link-local (169.254/16, fe80::/10), unique-local IPv6
// (fc00::/7) and multicast (224/4, ff00::/8) - traffic that stays on the local
// network. WAN: any other valid address. None: no peer address (unconnected
// UDP) or text that is not an address - not counted on either side.
//
// An IPv4-mapped IPv6 address (::ffff:10.0.0.1) is an IPv6 address here and so
// WAN, as QHostAddress::isInSubnet() treated it before this moved out of the
// GUI. The daemon never stores one: it records IPv4 peers as IPv4.
enum class HostZone { None, Lan, Wan };

HostZone host_zone(std::string_view ip);
inline bool is_lan(std::string_view ip) { return host_zone(ip) == HostZone::Lan; }

}  // namespace bandsight::core
