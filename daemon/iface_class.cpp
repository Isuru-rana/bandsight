// SPDX-License-Identifier: GPL-3.0-or-later
#include "iface_class.h"

#include <sys/stat.h>

#include <array>

namespace bandsight {
namespace {

bool path_exists(const std::string& p) {
    struct stat st{};
    return ::lstat(p.c_str(), &st) == 0;
}

bool starts_with_any(std::string_view name,
                     const std::array<std::string_view, 5>& prefixes) {
    for (std::string_view p : prefixes) {
        if (name.size() >= p.size() && name.compare(0, p.size(), p) == 0) return true;
    }
    return false;
}

}  // namespace

std::string_view to_string(IfaceKind kind) {
    switch (kind) {
        case IfaceKind::Physical: return "physical";
        case IfaceKind::Wifi:     return "wifi";
        case IfaceKind::Vpn:      return "vpn";
        case IfaceKind::Virtual:  return "virtual";
        case IfaceKind::Loopback: return "loopback";
    }
    return "virtual";
}

IfaceKind classify_iface(std::string_view name, const std::string& sysfs_root) {
    if (name == "lo") return IfaceKind::Loopback;

    const std::string base = sysfs_root + "/" + std::string(name);
    if (path_exists(base + "/phy80211") || path_exists(base + "/wireless")) {
        return IfaceKind::Wifi;
    }

    static constexpr std::array<std::string_view, 5> kVpnPrefixes{
        "tun", "tap", "wg", "tailscale", "ppp"};
    if (starts_with_any(name, kVpnPrefixes)) return IfaceKind::Vpn;

    if (path_exists(base + "/device")) return IfaceKind::Physical;

    return IfaceKind::Virtual;
}

bool counts_toward_totals(IfaceKind kind) {
    return kind == IfaceKind::Physical || kind == IfaceKind::Wifi;
}

bool snoop_dns(IfaceKind) { return true; }

}  // namespace bandsight
