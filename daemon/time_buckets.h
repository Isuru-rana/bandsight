// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace bandsight {

constexpr int kRes1s = 1;
constexpr int kRes1m = 60;
constexpr int kRes1h = 3600;
constexpr int kRes1d = 86400;

std::int64_t bucket_start(std::int64_t unix_ts, int res);
std::int64_t bucket_next(std::int64_t bucket_ts, int res);

}  // namespace bandsight
