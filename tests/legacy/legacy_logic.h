// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// See legacy_logic.cpp: frozen copies, for test_query_parity only.
#include <QString>
#include <QStringList>
#include <QVector>

#include "frame_types.h"
#include "host_class.h"
#include "units.h"

namespace bandsight::legacy {
void set_rate_unit(gui::RateUnit u);
void set_volume_unit(gui::VolumeUnit u);
QString format_rate(quint64 bytes_per_s);
QString format_volume(quint64 bytes);
QString service_name(int proto, int port);
QString port_label(int proto, int port);
gui::HostZone host_zone(const QString& ip);
QStringList disambiguated_labels(const QVector<gui::AppTotal>& apps);
}  // namespace bandsight::legacy
