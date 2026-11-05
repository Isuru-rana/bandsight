// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace bandsight::core {

// What the interface counters saw that a breakdown (Apps or Hosts) does not
// account for: virtual machines and forwarded traffic, kernel sockets such as
// WireGuard, containers, and anything an older daemon could not attribute.
//
// Each direction is clamped at zero on its own: the two sides are measured by
// different mechanisms, and a breakdown slightly over the headline in one
// direction must not hide a real gap in the other.
struct Unattributed {
    std::uint64_t down = 0;
    std::uint64_t up = 0;
    bool shown = false;   // worth a row: over 5% of the headline AND over 1 MB
    std::uint64_t total() const { return down + up; }
};

Unattributed unattributed(std::uint64_t headline_down, std::uint64_t headline_up,
                          std::uint64_t listed_down, std::uint64_t listed_up);

}  // namespace bandsight::core
