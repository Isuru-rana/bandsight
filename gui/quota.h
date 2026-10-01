// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QtGlobal>

namespace bandsight::gui {

// What the QuotaBar draws. cap == 0 means no cap is configured, in which case
// the bar shows usage without a target rather than a full or empty gauge.
struct QuotaInfo {
    quint64 used = 0;
    quint64 cap = 0;
    quint64 projected = 0;
    qint64 cycle_start = 0;
    qint64 cycle_end = 0;
    int cycle_day = 1;
};

// A billing cycle, as local wall-clock days (spec §7). Both ends are UTC
// timestamps of local midnight, because that is the boundary the daemon's
// res=86400 buckets already land on - anything else silently half-counts the
// first and last day of every cycle.
struct Cycle {
    qint64 start = 0;   // inclusive
    qint64 end = 0;     // exclusive
};

// The cycle containing `now`, for a cap that renews on `day` of each month.
// day is 1..28 (the daemon refuses anything else: a 30th does not exist in
// February).
Cycle cycle_for(qint64 now, int day);

// 0.0 at the start of the cycle, 1.0 at its end. Used to extrapolate.
double cycle_elapsed_fraction(const Cycle& c, qint64 now);

// Linear extrapolation of `used` to the end of the cycle. Returns `used`
// unchanged when the cycle has barely started, rather than a number divided by
// almost zero: a projection from four seconds of data is noise wearing a
// number's clothes.
quint64 project_to_cycle_end(quint64 used, const Cycle& c, qint64 now);

}  // namespace bandsight::gui
