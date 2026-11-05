// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/cycle.h"

#include <ctime>

namespace bandsight::core {

namespace {

// Local midnight on the given civil date, via mktime so the zone's own rules -
// including DST transitions and offsets that are not whole hours - decide what
// midnight means. tm_isdst = -1 asks mktime to work it out; hardcoding 0 shifts
// every cycle boundary by an hour for half the year.
std::int64_t local_midnight(int year, int month, int day) {
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_isdst = -1;
    return std::int64_t(std::mktime(&tm));
}

}  // namespace

Cycle cycle_for(std::int64_t now, int day) {
    if (day < 1) day = 1;
    if (day > 28) day = 28;

    std::time_t t = std::time_t(now);
    std::tm local{};
    localtime_r(&t, &local);

    int year = local.tm_year + 1900;
    int month = local.tm_mon + 1;

    // Before this month's renewal date, the current cycle began last month.
    if (local.tm_mday < day) {
        if (--month == 0) { month = 12; --year; }
    }

    int next_year = year, next_month = month + 1;
    if (next_month == 13) { next_month = 1; ++next_year; }

    return {local_midnight(year, month, day),
            local_midnight(next_year, next_month, day)};
}

double cycle_elapsed_fraction(const Cycle& c, std::int64_t now) {
    const std::int64_t span = c.end - c.start;
    if (span <= 0) return 1.0;
    if (now <= c.start) return 0.0;
    if (now >= c.end) return 1.0;
    return double(now - c.start) / double(span);
}

std::uint64_t project_to_cycle_end(std::uint64_t used, const Cycle& c, std::int64_t now) {
    const double f = cycle_elapsed_fraction(c, now);
    // Under an hour of a month is ~0.0014 of the cycle; dividing by it turns a
    // single large download into a projected terabyte. Report what is measured
    // until there is enough of the cycle to extrapolate from.
    if (f < 0.01) return used;
    return std::uint64_t(double(used) / f);
}

}  // namespace bandsight::core
