// SPDX-License-Identifier: GPL-3.0-or-later
#include "config_policy.h"

#include <cctype>
#include <cstdint>
#include <limits>

namespace bandsight {
namespace {

bool all_digits(const std::string& s) {
    if (s.empty()) return false;
    for (const unsigned char c : s)
        if (!std::isdigit(c)) return false;
    return true;
}

// Rejects before conversion rather than after: strtoull on 30 digits saturates
// at ULLONG_MAX and reports success through errno only, which is easy to miss.
bool fits_u64(const std::string& digits, std::uint64_t& out) {
    if (digits.size() > 20) return false;
    std::uint64_t v = 0;
    for (const char c : digits) {
        const std::uint64_t d = static_cast<std::uint64_t>(c - '0');
        if (v > (std::numeric_limits<std::uint64_t>::max() - d) / 10) return false;
        v = v * 10 + d;
    }
    out = v;
    return true;
}

}  // namespace

std::string validate_config(const std::string& key, const std::string& value) {
    if (key == "cap_bytes") {
        std::uint64_t bytes = 0;
        if (!all_digits(value) || !fits_u64(value, bytes))
            return "cap_bytes must be a non-negative integer";
        return {};   // 0 is meaningful: no cap set
    }
    if (key == "cycle_start_day") {
        std::uint64_t day = 0;
        if (!all_digits(value) || !fits_u64(value, day) || day < 1 || day > 28)
            // 28, not 31: a cycle starting on the 30th does not exist in
            // February, and the projection would have no start date to measure
            // from for one month in twelve.
            return "cycle_start_day must be between 1 and 28";
        return {};
    }
    return "unknown config key: " + key;
}

}  // namespace bandsight
