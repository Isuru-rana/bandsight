// SPDX-License-Identifier: GPL-3.0-or-later
// Bucket boundaries: every rollup and every history query depends on these
// agreeing, across DST changes and in zones whose offset is not whole hours.
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>

#include "check.h"
#include "time_buckets.h"

using namespace bandsight;

namespace {

bool use_zone(const char* tz) {
    if (!std::filesystem::exists(std::string("/usr/share/zoneinfo/") + tz)) return false;
    ::setenv("TZ", tz, 1);
    ::tzset();
    return true;
}

// Walks every hour bucket across [from, to): each must be distinct, strictly
// increasing, and contain the instants that floor to it.
void walk(std::int64_t from, std::int64_t to, int res) {
    std::int64_t b = bucket_start(from, res);
    int steps = 0;
    while (b < to && steps < 10000) {
        const std::int64_t n = bucket_next(b, res);
        CHECK(n > b);
        CHECK(bucket_start(b, res) == b);
        CHECK(bucket_start(n - 1, res) == b);   // the last second still belongs
        b = n;
        ++steps;
    }
    CHECK(steps < 10000);
}

void plain() {
    CHECK(bucket_start(1'700'000'059, kRes1m) == 1'700'000'040);
    CHECK(bucket_next(1'700'000'040, kRes1m) == 1'700'000'100);
    CHECK(bucket_start(1'700'000'059, kRes1s) == 1'700'000'059);
    bool threw = false;
    try { bucket_start(0, 7); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
}

void london() {
    if (!use_zone("Europe/London")) { std::printf("skipped: no Europe/London\n"); return; }
    // 2026-10-25 01:00-02:00 local happens twice. The two hours are distinct
    // buckets (B2: they once shared one row).
    const std::int64_t first = 1'792'887'600;   // 2026-10-25T00:20Z, 01:20 BST
    const std::int64_t second = first + 3600;   // 2026-10-25T01:20Z, 01:20 GMT
    CHECK(bucket_start(first, kRes1h) != bucket_start(second, kRes1h));
    CHECK(bucket_next(bucket_start(first, kRes1h), kRes1h) == bucket_start(second, kRes1h));
    // That day is 25 hours long; the spring one 23.
    const std::int64_t d25 = bucket_start(first, kRes1d);
    CHECK(bucket_next(d25, kRes1d) - d25 == 25 * 3600);
    const std::int64_t spring = bucket_start(1'774'746'000, kRes1d);   // 2026-03-29
    CHECK(bucket_next(spring, kRes1d) - spring == 23 * 3600);
    walk(1'774'600'000, 1'775'000'000, kRes1h);
    walk(1'792'800'000, 1'793'100'000, kRes1h);
    walk(1'770'000'000, 1'800'000'000, kRes1d);
}

void kolkata() {
    if (!use_zone("Asia/Kolkata")) { std::printf("skipped: no Asia/Kolkata\n"); return; }
    // UTC+5:30: hour buckets start at :30 UTC, never on the UTC hour.
    const std::int64_t b = bucket_start(1'700'000'000, kRes1h);
    CHECK((b + 19800) % 3600 == 0);
    CHECK(b % 3600 == 1800);
    const std::int64_t d = bucket_start(1'700'000'000, kRes1d);
    CHECK((d + 19800) % 86400 == 0);
    walk(1'700'000'000, 1'700'500'000, kRes1h);
}

}  // namespace

int main() {
    plain();
    london();
    kolkata();
    if (g_failures) std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    else std::printf("test_time: all checks passed\n");
    return g_failures ? 1 : 0;
}
