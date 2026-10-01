// SPDX-License-Identifier: GPL-3.0-or-later
#include "json_frame.h"

#include <cstdio>

namespace bandsight {

std::string escape_json(std::string_view in) {
    std::string out;
    out.reserve(in.size() + 8);
    for (char c : in) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[7];
                    std::snprintf(buf, sizeof(buf), "\\u%04x",
                                  static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

std::string frame(std::string_view body) {
    const std::uint32_t n = static_cast<std::uint32_t>(body.size());
    std::string out;
    out.reserve(4 + body.size());
    out.push_back(static_cast<char>(n & 0xFF));
    out.push_back(static_cast<char>((n >> 8) & 0xFF));
    out.push_back(static_cast<char>((n >> 16) & 0xFF));
    out.push_back(static_cast<char>((n >> 24) & 0xFF));
    out.append(body);
    return out;
}

std::string encode_hello(const std::string& db_path,
                         const std::vector<HelloIface>& ifaces) {
    std::string s = "{\"t\":\"hello\",\"ver\":1,\"db\":\"";
    s += escape_json(db_path);
    s += "\",\"ifaces\":[";
    for (std::size_t i = 0; i < ifaces.size(); ++i) {
        if (i) s += ',';
        s += "{\"id\":" + std::to_string(ifaces[i].id) + ",\"n\":\"" +
             escape_json(ifaces[i].name) + "\",\"kind\":\"" +
             escape_json(ifaces[i].kind) + "\"}";
    }
    s += "]}";
    return s;
}

std::string encode_sample(std::int64_t ts, const std::vector<FrameIface>& ifaces,
                          const std::vector<FrameApp>& apps) {
    std::string s = "{\"t\":\"sample\",\"ts\":" + std::to_string(ts) + ",\"ifaces\":[";
    for (std::size_t i = 0; i < ifaces.size(); ++i) {
        if (i) s += ',';
        s += "{\"id\":" + std::to_string(ifaces[i].id) +
             ",\"rx\":" + std::to_string(ifaces[i].rx) +
             ",\"tx\":" + std::to_string(ifaces[i].tx) + "}";
    }
    s += "],\"apps\":[";
    for (std::size_t i = 0; i < apps.size(); ++i) {
        if (i) s += ',';
        s += "{\"id\":" + std::to_string(apps[i].id) +
             ",\"rx\":" + std::to_string(apps[i].rx) +
             ",\"tx\":" + std::to_string(apps[i].tx) +
             ",\"iface\":" + std::to_string(apps[i].iface) + "}";
    }
    s += "]}";
    return s;
}

std::string encode_pong() { return "{\"t\":\"pong\"}"; }

std::string encode_ok() { return "{\"t\":\"ok\"}"; }

std::string json_string_field(const std::string& line, const std::string& field) {
    const std::string needle = "\"" + field + "\"";
    std::size_t at = 0;
    for (;;) {
        at = line.find(needle, at);
        if (at == std::string::npos) return {};
        // "mykey" contains "key"; only a quote or the start of the object may
        // precede the opening quote of a field name.
        const bool starts_field = (at == 0) || line[at - 1] == '{' ||
                                  line[at - 1] == ',' || line[at - 1] == ' ';
        std::size_t p = at + needle.size();
        while (p < line.size() && line[p] == ' ') ++p;
        if (starts_field && p < line.size() && line[p] == ':') {
            ++p;
            while (p < line.size() && line[p] == ' ') ++p;
            if (p >= line.size() || line[p] != '"') return {};   // not a string
            const std::size_t start = p + 1;
            const std::size_t end = line.find('"', start);
            if (end == std::string::npos) return {};             // unterminated
            return line.substr(start, end - start);
        }
        at += needle.size();
    }
}

std::string encode_error(const std::string& msg) {
    return "{\"t\":\"error\",\"msg\":\"" + escape_json(msg) + "\"}";
}

}  // namespace bandsight
