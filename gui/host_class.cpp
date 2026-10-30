// SPDX-License-Identifier: GPL-3.0-or-later
#include "host_class.h"

#include <QHostAddress>

namespace bandsight::gui {

HostZone host_zone(const QString& ip) {
    if (ip.isEmpty()) return HostZone::None;
    const QHostAddress a(ip);
    if (a.isNull()) return HostZone::None;

    static const QPair<QHostAddress, int> kLan[] = {
        QHostAddress::parseSubnet(QStringLiteral("10.0.0.0/8")),
        QHostAddress::parseSubnet(QStringLiteral("172.16.0.0/12")),
        QHostAddress::parseSubnet(QStringLiteral("192.168.0.0/16")),
        QHostAddress::parseSubnet(QStringLiteral("169.254.0.0/16")),
        QHostAddress::parseSubnet(QStringLiteral("224.0.0.0/4")),
        QHostAddress::parseSubnet(QStringLiteral("fe80::/10")),
        QHostAddress::parseSubnet(QStringLiteral("fc00::/7")),
        QHostAddress::parseSubnet(QStringLiteral("ff00::/8")),
    };
    for (const auto& net : kLan)
        if (a.isInSubnet(net)) return HostZone::Lan;
    return HostZone::Wan;
}

}  // namespace bandsight::gui
