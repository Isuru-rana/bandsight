// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>

namespace bandsight::gui {

// Spec §5: speed defaults to bits per second and volume to bytes, both
// switchable, because mixing the two up is the most common complaint levelled
// at bandwidth tools. The preference is process-wide rather than threaded
// through every call site: it is a display choice that must be identical
// everywhere at once, and a per-widget copy is exactly how a graph ends up
// disagreeing with the table beside it.
enum class RateUnit { Bits, Bytes };     // Mbit/s | MB/s
enum class VolumeUnit { Decimal, Binary };  // GB (10^9) | GiB (2^30)

void set_rate_unit(RateUnit u);
void set_volume_unit(VolumeUnit u);
RateUnit rate_unit();
VolumeUnit volume_unit();

QString format_rate(quint64 bytes_per_s);   // kbit/s | Mbit/s | Gbit/s, or /s bytes
QString format_volume(quint64 bytes);       // B | kB | MB | GB | TB, or KiB | MiB…
}  // namespace bandsight::gui
