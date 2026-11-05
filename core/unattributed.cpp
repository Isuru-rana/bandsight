// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/unattributed.h"

namespace bandsight::core {

namespace {
constexpr std::uint64_t kMinBytes = 1'000'000;   // the "> 1 MB" floor
}

Unattributed unattributed(std::uint64_t headline_down, std::uint64_t headline_up,
                          std::uint64_t listed_down, std::uint64_t listed_up) {
    Unattributed u;
    u.down = headline_down > listed_down ? headline_down - listed_down : 0;
    u.up = headline_up > listed_up ? headline_up - listed_up : 0;
    const std::uint64_t headline = headline_down + headline_up;
    // gap * 20 > headline is gap > 5% without floating point.
    u.shown = u.total() > kMinBytes && u.total() * 20 > headline;
    return u;
}

}  // namespace bandsight::core
