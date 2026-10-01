// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <sqlite3.h>

#include <cstdint>

namespace bandsight {

// Rolls every bucket of src_res that is fully in the past into dst_res, then
// advances the dst_res watermark in rollup_state. Idempotent; one transaction.
void rollup_tier(sqlite3* db, int src_res, int dst_res, std::int64_t now);

// Deletes rows past their tier's retention. The 86400 tier is kept forever.
void enforce_retention(sqlite3* db, std::int64_t now);

// Reclaims freed pages. Only effective because Store's constructor sets
// PRAGMA auto_vacuum=INCREMENTAL before the schema is created; call this
// roughly once a day (alongside the daily rollup), not every tick.
void vacuum_incremental(sqlite3* db);

}  // namespace bandsight
