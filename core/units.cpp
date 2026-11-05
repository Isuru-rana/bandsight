// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/units.h"

#include <cstdio>

namespace bandsight::core {

std::string format_fixed(double v, int prec) {
    // 80 decimals is the exact expansion of every value formatted here: the
    // smallest is one byte over 10^3 (about 2^-10), whose expansion ends 62
    // digits after the point.
    char buf[512];
    std::snprintf(buf, sizeof buf, "%.80f", v);
    const std::string s(buf);
    const std::size_t dot = s.find('.');
    std::string digits = s.substr(0, dot) + s.substr(dot + 1, std::size_t(prec));
    if (s[dot + 1 + std::size_t(prec)] >= '5') {
        int i = int(digits.size()) - 1;
        for (; i >= 0; --i) {
            if (digits[std::size_t(i)] == '9') {
                digits[std::size_t(i)] = '0';
            } else {
                ++digits[std::size_t(i)];
                break;
            }
        }
        if (i < 0) digits.insert(digits.begin(), '1');
    }
    const std::size_t int_len = digits.size() - std::size_t(prec);
    std::string out = digits.substr(0, int_len);
    if (prec > 0) out += '.' + digits.substr(int_len);
    return out;
}

std::string format_rate(std::uint64_t bytes_per_s, RateUnit u) {
    if (u == RateUnit::Bytes) {
        const double b = double(bytes_per_s);
        if (b >= 1e9) return format_fixed(b / 1e9, 2) + " GB/s";
        if (b >= 1e6) return format_fixed(b / 1e6, 1) + " MB/s";
        if (b == 0.0) return "0 kB/s";
        return format_fixed(b / 1e3, 1) + " kB/s";
    }
    const double bits = double(bytes_per_s) * 8.0;   // the ONLY factor-of-8 in the codebase
    if (bits >= 1e9) return format_fixed(bits / 1e9, 2) + " Gb/s";
    if (bits >= 1e6) return format_fixed(bits / 1e6, 1) + " Mb/s";
    if (bits == 0.0) return "0 kb/s";
    return format_fixed(bits / 1e3, 1) + " kb/s";
}

std::string format_volume(std::uint64_t bytes, VolumeUnit u) {
    const double b = double(bytes);
    if (u == VolumeUnit::Binary) {
        constexpr double Ki = 1024.0, Mi = Ki * 1024, Gi = Mi * 1024, Ti = Gi * 1024;
        if (b >= Ti) return format_fixed(b / Ti, 2) + " TiB";
        if (b >= Gi) return format_fixed(b / Gi, 2) + " GiB";
        if (b >= Mi) return format_fixed(b / Mi, 1) + " MiB";
        if (b >= Ki) return format_fixed(b / Ki, 1) + " KiB";
        return std::to_string(bytes) + " B";
    }
    // Volume in decimal SI (10^9 per unit), not binary (2^30). This matches how
    // ISPs and data caps are conventionally quoted, and is why decimal is the
    // default rather than the other way round.
    if (b >= 1e12) return format_fixed(b / 1e12, 2) + " TB";
    if (b >= 1e9)  return format_fixed(b / 1e9, 2) + " GB";
    if (b >= 1e6)  return format_fixed(b / 1e6, 1) + " MB";
    if (b >= 1e3)  return format_fixed(b / 1e3, 1) + " kB";
    return std::to_string(bytes) + " B";
}

}  // namespace bandsight::core
