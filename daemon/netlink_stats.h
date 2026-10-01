// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace bandsight {

struct IfaceStats {
    int ifindex = 0;
    std::string name;
    std::uint64_t rx_bytes = 0;
    std::uint64_t tx_bytes = 0;
};

// One RTM_GETLINK dump. Throws std::system_error if the netlink socket fails.
std::vector<IfaceStats> read_iface_stats();

}  // namespace bandsight
