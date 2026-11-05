// SPDX-License-Identifier: GPL-3.0-or-later
// The GUI's formatting, service names, LAN/WAN rule and app labels exactly as
// they were before they moved to core/, renamed into bandsight::legacy and kept
// only as the reference test_query_parity compares the wrappers against.
#include <QCoreApplication>
#include <QHash>
#include <QHostAddress>
#include <QSet>
#include <QString>
#include <QStringList>

#include "frame_types.h"
#include "host_class.h"
#include "units.h"

#include "legacy_logic.h"


namespace bandsight::legacy {
using namespace bandsight::gui;

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
    if (bits >= 1e9) return QString::number(bits / 1e9, 'f', 2) + " Gb/s";
    if (bits >= 1e6) return QString::number(bits / 1e6, 'f', 1) + " Mb/s";
    if (bits == 0.0) return QStringLiteral("0 kb/s");
    return QString::number(bits / 1e3, 'f', 1) + " kb/s";
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

}  // namespace bandsight::legacy


namespace bandsight::legacy {
using namespace bandsight::gui;

namespace {
QString tr(const char* s) { return QCoreApplication::translate("Services", s); }
}  // namespace

QString port_label(int proto, int port) {
    return QStringLiteral("%1/%2").arg(port).arg(
        proto == 17 ? QStringLiteral("udp") : QStringLiteral("tcp"));
}

// The daemon prefers the ports named here when it picks a socket's service
// side (is_known_service_port in daemon/traffic_type.cpp); the two lists are
// kept in step, and a test checks it.
QString service_name(int proto, int port) {
    const bool udp = proto == 17;
    switch (port) {
    case 0: return tr("Other");
    case 443: return udp ? tr("QUIC") : tr("HTTPS");
    case 80: return tr("HTTP");
    case 53: return tr("DNS");
    case 853: return udp ? tr("DNS-over-QUIC") : tr("DNS-over-TLS");
    case 22: return tr("SSH");
    case 123: return tr("NTP");
    case 25: case 465: case 587: case 143: case 993: case 995: return tr("Mail");
    case 1883: case 8883: return tr("MQTT");
    case 3478: return tr("WebRTC/STUN");
    default: break;
    }
    if (udp) {
        if (port == 5353) return tr("mDNS");
        if (port == 41641) return tr("Tailscale");
        if (port == 51820) return tr("WireGuard");
        if (port >= 19302 && port <= 19309) return tr("WebRTC/STUN");
    }
    return port_label(proto, port);
}

}  // namespace bandsight::legacy


namespace bandsight::legacy {
using namespace bandsight::gui;

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

}  // namespace bandsight::legacy


namespace bandsight::legacy {
using namespace bandsight::gui;

namespace {

// Past this the label is longer than anything a column can show, and the tooltip
// is the honest place for the rest.
constexpr int kMaxComponents = 3;

QStringList dir_components(const QString& exe) {
    QStringList parts = exe.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (!parts.isEmpty()) parts.removeLast();   // drop the binary itself
    return parts;
}

QString tail(const QStringList& parts, int n) {
    const int take = qMin(n, int(parts.size()));
    return QStringList(parts.mid(parts.size() - take)).join(QLatin1Char('/'));
}

}  // namespace

QStringList disambiguated_labels(const QVector<AppTotal>& apps) {
    QStringList out;
    out.reserve(apps.size());
    for (const AppTotal& a : apps) out << a.display_name();

    // Which rows share a name. Only those are touched; every other label is
    // exactly what it was.
    QHash<QString, QVector<int>> by_name;
    for (int i = 0; i < out.size(); ++i) by_name[out[i]].push_back(i);

    for (auto it = by_name.begin(); it != by_name.end(); ++it) {
        const QVector<int>& rows = it.value();
        if (rows.size() < 2) continue;

        QVector<QStringList> dirs;
        for (int r : rows) dirs.push_back(dir_components(apps[r].exe));

        // The shortest tail that separates them all. Two nvm installs differ
        // only near the root, so this can run out - hence the cap and the
        // fallback below.
        int depth = 1;
        for (; depth <= kMaxComponents; ++depth) {
            QSet<QString> seen;
            bool unique = true;
            for (const QStringList& d : dirs) {
                const QString t = tail(d, depth);
                if (seen.contains(t)) { unique = false; break; }
                seen.insert(t);
            }
            if (unique) break;
        }

        // The cap can leave rows still identical - two nvm installs share
        // "node/v24.18.0/bin" and differ only near the root, which is exactly
        // what the cap refuses to spell out. Found against the live database,
        // where both rendered as "node (node/v24.18.0/bin)".
        //
        // Those fall back to the whole directory, which ALWAYS separates them:
        // rows collide only when they share a name, and a shared name in a
        // shared directory would mean the same executable - which cannot happen,
        // since exe is the identity of the row. Long, but the column elides and
        // the tooltip carries the full path anyway. An earlier attempt spliced
        // in the first differing component instead; it produced "usr/.../usr/bin"
        // when that component was already in the tail, and still left duplicates.
        const bool use_full_dir = depth > kMaxComponents;

        for (int i = 0; i < rows.size(); ++i) {
            const QString suffix = use_full_dir
                                       ? dirs[i].join(QLatin1Char('/'))
                                       : tail(dirs[i], depth);
            // A row whose executable is unknown has no directory to show; it
            // keeps the bare name rather than gaining an empty bracket.
            if (suffix.isEmpty()) continue;
            out[rows[i]] = QStringLiteral("%1 (%2)").arg(out[rows[i]], suffix);
        }
    }
    return out;
}

}  // namespace bandsight::legacy
