// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <string_view>

namespace bandsight {

enum class IfaceKind { Physical, Wifi, Vpn, Virtual, Loopback };

std::string_view to_string(IfaceKind kind);

// sysfs_root is injectable so tests can supply a fixture tree.
IfaceKind classify_iface(std::string_view name,
                         const std::string& sysfs_root = "/sys/class/net");

// False for Loopback, Virtual and Vpn: tunnel bytes are also counted on the
// physical interface underneath, so counting both double-counts.
bool counts_toward_totals(IfaceKind kind);

// Always true. Separate from accounting on purpose: lo carries the
// systemd-resolved stub leg, the only source of per-app domain attribution.
bool snoop_dns(IfaceKind);

}  // namespace bandsight
