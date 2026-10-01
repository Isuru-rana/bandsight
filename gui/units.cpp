// SPDX-License-Identifier: GPL-3.0-or-later
#include "units.h"

namespace bandsight::gui {

namespace {
RateUnit g_rate = RateUnit::Bits;
VolumeUnit g_volume = VolumeUnit::Decimal;
}  // namespace

void set_rate_unit(RateUnit u) { g_rate = u; }
void set_volume_unit(VolumeUnit u) { g_volume = u; }
RateUnit rate_unit() { return g_rate; }
VolumeUnit volume_unit() { return g_volume; }

QString format_rate(quint64 bytes_per_s) {
    if (g_rate == RateUnit::Bytes) {
        const double b = double(bytes_per_s);
        if (b >= 1e9) return QString::number(b / 1e9, 'f', 2) + " GB/s";
        if (b >= 1e6) return QString::number(b / 1e6, 'f', 1) + " MB/s";
        if (b == 0.0) return QStringLiteral("0 kB/s");
        return QString::number(b / 1e3, 'f', 1) + " kB/s";
    }
    const double bits = double(bytes_per_s) * 8.0;   // the ONLY factor-of-8 in the codebase
    if (bits >= 1e9) return QString::number(bits / 1e9, 'f', 2) + " Gbit/s";
    if (bits >= 1e6) return QString::number(bits / 1e6, 'f', 1) + " Mbit/s";
    if (bits == 0.0) return QStringLiteral("0 kbit/s");
    return QString::number(bits / 1e3, 'f', 1) + " kbit/s";
}

QString format_volume(quint64 bytes) {
    if (g_volume == VolumeUnit::Binary) {
        const double b = double(bytes);
        if (b >= 1024.0 * 1024 * 1024 * 1024) return QString::number(b / (1024.0*1024*1024*1024), 'f', 2) + " TiB";
        if (b >= 1024.0 * 1024 * 1024) return QString::number(b / (1024.0*1024*1024), 'f', 2) + " GiB";
        if (b >= 1024.0 * 1024) return QString::number(b / (1024.0*1024), 'f', 1) + " MiB";
        if (b >= 1024.0) return QString::number(b / 1024.0, 'f', 1) + " KiB";
        return QString::number(bytes) + " B";
    }
    // Volume in decimal SI (10^9 per unit), not binary (2^30).
    // This matches how ISPs and data caps are conventionally quoted, and is why
    // decimal is the default rather than the other way round.
    const double b = double(bytes);
    if (b >= 1e12) return QString::number(b / 1e12, 'f', 2) + " TB";
    if (b >= 1e9)  return QString::number(b / 1e9, 'f', 2) + " GB";
    if (b >= 1e6)  return QString::number(b / 1e6, 'f', 1) + " MB";
    if (b >= 1e3)  return QString::number(b / 1e3, 'f', 1) + " kB";
    return QString::number(bytes) + " B";
}

}  // namespace bandsight::gui
