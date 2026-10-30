// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>

namespace bandsight::gui {

// Where a remote host is, by its address alone. LAN: private IPv4 (10/8,
// 172.16/12, 192.168/16), link-local (169.254/16, fe80::/10), unique-local IPv6
// (fc00::/7) and multicast (224/4, ff00::/8) - traffic that stays on the local
// network. WAN: any other valid address. None: no peer address (unconnected
// UDP) or text that is not an address - not counted on either side.
enum class HostZone { None, Lan, Wan };

HostZone host_zone(const QString& ip);
inline bool is_lan(const QString& ip) { return host_zone(ip) == HostZone::Lan; }

}  // namespace bandsight::gui
