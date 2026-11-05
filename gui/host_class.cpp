// SPDX-License-Identifier: GPL-3.0-or-later
#include "host_class.h"

#include "core/host_zone.h"

namespace bandsight::gui {

HostZone host_zone(const QString& ip) {
    switch (core::host_zone(ip.toStdString())) {
    case core::HostZone::Lan: return HostZone::Lan;
    case core::HostZone::Wan: return HostZone::Wan;
    case core::HostZone::None: break;
    }
    return HostZone::None;
}

}  // namespace bandsight::gui
