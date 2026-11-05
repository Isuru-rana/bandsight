// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/frames.h"

#include "json_value.h"

namespace bandsight::core {

namespace {

std::string str_field(const JsonValue& o, std::string_view key) {
    const JsonValue* v = o.get(key);
    return v && v->type == JsonValue::Type::String ? v->str : std::string();
}

double num_field(const JsonValue& o, std::string_view key) {
    const JsonValue* v = o.get(key);
    return v && v->type == JsonValue::Type::Number ? v->num : 0.0;
}

const JsonValue* array_field(const JsonValue& o, std::string_view key) {
    const JsonValue* v = o.get(key);
    return v && v->type == JsonValue::Type::Array ? v : nullptr;
}

}  // namespace

bool extract_frames(std::string& buf, std::vector<std::string>& out) {
    std::size_t at = 0;
    for (;;) {
        if (buf.size() - at < 4) break;
        const auto* p = reinterpret_cast<const unsigned char*>(buf.data() + at);
        const std::uint32_t len = std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 |
                                  std::uint32_t(p[2]) << 16 | std::uint32_t(p[3]) << 24;
        if (len > kMaxFrameBytes) return false;
        if (buf.size() - at - 4 < len) break;
        out.push_back(buf.substr(at + 4, len));
        at += 4 + len;
    }
    buf.erase(0, at);
    return true;
}

std::string frame_type(std::string_view body) {
    const auto v = parse_json(body);
    return v ? str_field(*v, "t") : std::string();
}

std::optional<Hello> decode_hello(std::string_view body) {
    const auto v = parse_json(body);
    if (!v || str_field(*v, "t") != "hello") return std::nullopt;
    if (!v->get("db") || !v->get("ifaces")) return std::nullopt;
    Hello h;
    h.db_path = str_field(*v, "db");
    // Optional and additive: older daemons do not send it, and anything but a
    // string is treated as absent rather than refusing the frame.
    h.version = str_field(*v, "version");
    if (const JsonValue* a = array_field(*v, "ifaces"))
        for (const JsonValue& i : a->arr)
            h.ifaces.push_back({int(num_field(i, "id")), str_field(i, "n"), str_field(i, "kind")});
    return h;
}

std::optional<Sample> decode_sample(std::string_view body) {
    const auto v = parse_json(body);
    if (!v || str_field(*v, "t") != "sample") return std::nullopt;
    if (!v->get("ts") || !v->get("ifaces")) return std::nullopt;
    Sample s;
    s.ts = std::int64_t(num_field(*v, "ts"));
    if (const JsonValue* a = array_field(*v, "ifaces"))
        for (const JsonValue& i : a->arr)
            s.ifaces.push_back({int(num_field(i, "id")), std::uint64_t(num_field(i, "rx")),
                                std::uint64_t(num_field(i, "tx"))});
    return s;
}

}  // namespace bandsight::core
