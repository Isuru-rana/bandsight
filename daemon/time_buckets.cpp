// SPDX-License-Identifier: GPL-3.0-or-later
#include "time_buckets.h"

#include <ctime>
#include <stdexcept>

namespace bandsight {
namespace {

std::int64_t utc_offset_at(std::int64_t ts) {
    std::time_t t = static_cast<std::time_t>(ts);
    std::tm tm{};
    localtime_r(&t, &tm);
    return static_cast<std::int64_t>(tm.tm_gmtoff);
}

std::int64_t floor_div(std::int64_t a, std::int64_t b) {
    const std::int64_t q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

// Floors a timestamp to the start of its local hour or local day, by shifting
// into local seconds with the UTC offset in force AT that instant and shifting
// back - never by rebuilding the instant from wall-clock fields.
//
// Rebuilding is what B2 was. mktime with tm_isdst = -1 has to resolve an
// ambiguous local time to one answer, and at a fall-back BOTH repeated hours are
// the same wall clock: they floored to the same bucket, so one res=3600 row held
// two hours of traffic. The daemon persists that row, so it was wrong in the
// database and not merely on screen. Using the offset at the instant keeps the
// two hours distinct, because their offsets differ by exactly the hour that was
// repeated.
//
// The offset can differ between ts and the bucket it lands in - a transition
// earlier the same day - so the result is recomputed once with the offset that
// actually applies there. One pass suffices: no zone changes offset twice within
// a single bucket.
std::int64_t local_floor(std::int64_t ts, std::int64_t res) {
    const std::int64_t off = utc_offset_at(ts);

    // Where the bucket begins in LOCAL seconds. This part is exact whatever the
    // offset does later, because it only asks which local hour or day `ts` falls
    // in.
    const std::int64_t local_start = floor_div(ts + off, res) * res;

    // Converting that back needs the offset in force AT THE BOUNDARY, which on a
    // 25-hour day is not the offset at `ts`: an instant late on such a day floors
    // to a boundary an hour on the other side of the transition. Re-flooring with
    // the boundary's offset (rather than re-converting) lands an hour early, so
    // the conversion is what gets corrected, once.
    std::int64_t b = local_start - off;
    const std::int64_t off_b = utc_offset_at(b);
    if (off_b != off) b = local_start - off_b;
    return b;
}

}  // namespace

std::int64_t bucket_start(std::int64_t unix_ts, int res) {
    switch (res) {
        case kRes1s:
        case kRes1m:
            return unix_ts - (unix_ts % res);
        case kRes1h:
            return local_floor(unix_ts, kRes1h);
        case kRes1d:
            return local_floor(unix_ts, kRes1d);
        default:
            throw std::invalid_argument("bucket_start: unsupported resolution");
    }
}

std::int64_t bucket_next(std::int64_t bucket_ts, int res) {
    if (res == kRes1s || res == kRes1m) return bucket_ts + res;
    if (res != kRes1h && res != kRes1d)
        throw std::invalid_argument("bucket_next: unsupported resolution");

    // The next DISTINCT bucket, not a fixed offset. A long day is 25 hours and a
    // short one 23, so adding 86400 to a local midnight can land back inside the
    // same day - which would stall a rollup loop that walks buckets - or skip
    // one entirely. Probing forward and re-flooring cannot do either.
    std::int64_t probe = bucket_ts + res;
    std::int64_t next = bucket_start(probe, res);
    while (next <= bucket_ts) {
        probe += kRes1h;   // the largest step any zone shifts by
        next = bucket_start(probe, res);
    }
    return next;
}

}  // namespace bandsight
