// SPDX-License-Identifier: GPL-3.0-or-later
#include "units.h"

#include "core/units.h"

namespace bandsight::gui {

namespace {
RateUnit g_rate = RateUnit::Bits;
VolumeUnit g_volume = VolumeUnit::Decimal;
}  // namespace

void set_rate_unit(RateUnit u) { g_rate = u; }
void set_volume_unit(VolumeUnit u) { g_volume = u; }
RateUnit rate_unit() { return g_rate; }
VolumeUnit volume_unit() { return g_volume; }

// The formatting itself is core::format_rate / core::format_volume, shared with
// the command-line client; only the preference is the GUI's.
QString format_rate(quint64 bytes_per_s) {
    return QString::fromStdString(core::format_rate(
        bytes_per_s, g_rate == RateUnit::Bytes ? core::RateUnit::Bytes : core::RateUnit::Bits));
}

QString format_volume(quint64 bytes) {
    return QString::fromStdString(core::format_volume(
        bytes, g_volume == VolumeUnit::Binary ? core::VolumeUnit::Binary
                                              : core::VolumeUnit::Decimal));
}

}  // namespace bandsight::gui
