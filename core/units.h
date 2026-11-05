// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <string>

namespace bandsight::core {

// Spec §5: speed defaults to bits per second and volume to decimal bytes, both
// switchable. These are the one implementation; the GUI's format_rate and
// format_volume (gui/units.h) wrap them with its process-wide preference.
enum class RateUnit { Bits, Bytes };        // Mb/s | MB/s
enum class VolumeUnit { Decimal, Binary };  // GB (10^9) | GiB (2^30)

std::string format_rate(std::uint64_t bytes_per_s, RateUnit u);  // kb/s | Mb/s | Gb/s, or kB/s | MB/s | GB/s
std::string format_volume(std::uint64_t bytes, VolumeUnit u);    // B | kB | MB | GB | TB, or KiB | MiB...

// v with `prec` decimals, rounded half away from zero on v's exact binary
// value. That is what QString::number(v, 'f', prec) does and printf does not
// (printf rounds an exact tie to even: 1.25 -> "1.2", where Qt gives "1.3"), so
// the GUI's output stays byte-for-byte what it was before this was shared.
// For non-negative v; every caller formats a byte count.
std::string format_fixed(double v, int prec);

}  // namespace bandsight::core
