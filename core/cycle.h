// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace bandsight::core {

// A billing cycle, as local wall-clock days (spec §7). Both ends are UTC
// timestamps of local midnight, because that is the boundary the daemon's
// res=86400 buckets already land on - anything else silently half-counts the
// first and last day of every cycle.
struct Cycle {
    std::int64_t start = 0;   // inclusive
    std::int64_t end = 0;     // exclusive
};

// The cycle containing `now`, for a cap that renews on `day` of each month.
// day is 1..28 (the daemon refuses anything else: a 30th does not exist in
// February).
Cycle cycle_for(std::int64_t now, int day);

// 0.0 at the start of the cycle, 1.0 at its end. Used to extrapolate.
double cycle_elapsed_fraction(const Cycle& c, std::int64_t now);

// Linear extrapolation of `used` to the end of the cycle. Returns `used`
// unchanged when the cycle has barely started, rather than a number divided by
// almost zero: a projection from four seconds of data is noise wearing a
// number's clothes.
std::uint64_t project_to_cycle_end(std::uint64_t used, const Cycle& c, std::int64_t now);

}  // namespace bandsight::core
