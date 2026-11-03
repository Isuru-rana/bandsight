// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>

namespace bandsight::gui {

// What a (protocol, service port) pair from type_samples is called on the
// Traffic type card: proto 6 is TCP, 17 UDP; port 0 is the daemon's "other"
// bucket. Several pairs share a name (DNS over TCP and UDP, the mail ports).
// A port with no name here reads as itself, e.g. "8080/tcp".
QString service_name(int proto, int port);

// "443/tcp", "53/udp": how one pair is listed in a row's tooltip.
QString port_label(int proto, int port);

}  // namespace bandsight::gui
