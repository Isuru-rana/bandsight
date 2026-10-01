// SPDX-License-Identifier: GPL-3.0-or-later
#include "frame_codec.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace bandsight::gui {

std::optional<std::vector<QByteArray>> extract_frames(QByteArray& buf) {
    std::vector<QByteArray> out;
    for (;;) {
        if (buf.size() < 4) return out;
        const auto* p = reinterpret_cast<const uchar*>(buf.constData());
        const quint32 len = quint32(p[0]) | quint32(p[1]) << 8 |
                            quint32(p[2]) << 16 | quint32(p[3]) << 24;
        if (len > kMaxFrameBytes) return std::nullopt;
        if (buf.size() < qsizetype(4 + len)) return out;
        out.push_back(buf.mid(4, len));
        buf.remove(0, 4 + len);
    }
}

QString frame_type(const QByteArray& body) {
    return QJsonDocument::fromJson(body).object().value("t").toString();
}

std::optional<HelloInfo> decode_hello(const QByteArray& body) {
    const QJsonObject o = QJsonDocument::fromJson(body).object();
    if (o.value("t").toString() != QLatin1String("hello")) return std::nullopt;
    if (!o.contains("db") || !o.contains("ifaces")) return std::nullopt;
    HelloInfo h;
    h.db_path = o.value("db").toString();
    for (const auto& v : o.value("ifaces").toArray()) {
        const QJsonObject i = v.toObject();
        h.ifaces.push_back({i.value("id").toInt(),
                            i.value("n").toString(),
                            i.value("kind").toString()});
    }
    return h;
}

QString decode_error(const QByteArray& body) {
    return QJsonDocument::fromJson(body).object().value("msg").toString();
}

std::optional<SampleFrame> decode_sample(const QByteArray& body) {
    const QJsonObject o = QJsonDocument::fromJson(body).object();
    if (o.value("t").toString() != QLatin1String("sample")) return std::nullopt;
    if (!o.contains("ts") || !o.contains("ifaces")) return std::nullopt;
    SampleFrame s;
    s.ts = qint64(o.value("ts").toDouble());
    for (const auto& v : o.value("ifaces").toArray()) {
        const QJsonObject i = v.toObject();
        s.ifaces.push_back({i.value("id").toInt(),
                            quint64(i.value("rx").toDouble()),
                            quint64(i.value("tx").toDouble())});
    }
    return s;
}

}  // namespace bandsight::gui
