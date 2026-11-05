// SPDX-License-Identifier: GPL-3.0-or-later
#include "output.h"

#include <algorithm>
#include <cstdio>
#include <ctime>

namespace bandsight::cli {

std::string Style::bold(const std::string& s) const { return fancy ? "\033[1m" + s + "\033[0m" : s; }
std::string Style::dim(const std::string& s) const { return fancy ? "\033[2m" + s + "\033[0m" : s; }
std::string Style::down(const std::string& s) const { return fancy ? "\033[34m" + s + "\033[0m" : s; }
std::string Style::up(const std::string& s) const { return fancy ? "\033[33m" + s + "\033[0m" : s; }

std::size_t text_width(const std::string& s) {
    std::size_t n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) ++n;
    return n;
}

Table::Table(std::vector<std::string> header, std::size_t left_cols)
    : header_(std::move(header)), left_(left_cols) {}

void Table::add(std::vector<std::string> cells, std::uint64_t down, std::uint64_t up, bool muted) {
    cells.resize(header_.size());
    rows_.push_back({std::move(cells), down, up, muted});
}

std::string Table::render(const Style& st, bool bars) const {
    std::vector<std::size_t> w(header_.size(), 0);
    for (std::size_t c = 0; c < header_.size(); ++c) {
        w[c] = text_width(header_[c]);
        for (const Row& r : rows_) w[c] = std::max(w[c], text_width(r.cells[c]));
    }
    const auto pad = [&](const std::string& s, std::size_t c) {
        const std::string fill(w[c] - text_width(s), ' ');
        return c < left_ ? s + fill : fill + s;
    };
    const auto join = [&](const std::vector<std::string>& cells) {
        std::string line;
        for (std::size_t c = 0; c < cells.size(); ++c) {
            if (c) line += "  ";
            line += pad(cells[c], c);
        }
        // No trailing blanks from an empty last left-aligned column.
        while (!line.empty() && line.back() == ' ') line.pop_back();
        return line;
    };

    std::uint64_t biggest = 0;
    for (const Row& r : rows_) biggest = std::max(biggest, r.down + r.up);
    constexpr int kBar = 20;

    std::string out = st.bold(join(header_)) + "\n";
    for (const Row& r : rows_) {
        std::string line = join(r.cells);
        if (r.muted) line = st.dim(line);
        if (bars && biggest > 0) {
            // Down then up, each rounded on its own, at least one cell for a
            // direction that moved anything.
            const auto cells = [&](std::uint64_t v) {
                if (v == 0) return 0;
                return std::max(1, int((double(v) / double(biggest)) * kBar + 0.5));
            };
            const int d = cells(r.down), u = cells(r.up);
            std::string bd, bu;
            for (int i = 0; i < d; ++i) bd += "█";
            for (int i = 0; i < u; ++i) bu += "█";
            // Pad the plain line to the table width first, so bars line up.
            std::size_t width = 0;
            for (std::size_t c = 0; c < w.size(); ++c) width += w[c] + (c ? 2 : 0);
            const std::size_t plain = text_width(join(r.cells));
            line += std::string(width - std::min(width, plain), ' ') + "  " + st.down(bd) + st.up(bu);
        }
        out += line + "\n";
    }
    return out;
}

namespace {
std::string strf(const char* fmt, std::int64_t t) {
    const std::time_t tt = std::time_t(t);
    std::tm tm{};
    localtime_r(&tt, &tm);
    char buf[64];
    std::strftime(buf, sizeof buf, fmt, &tm);
    return buf;
}
}  // namespace

std::string local_minutes(std::int64_t t) { return strf("%Y-%m-%d %H:%M", t); }

std::string local_date(std::int64_t t) {
    std::string s = strf("%d %b %Y", t);
    if (!s.empty() && s[0] == '0') s.erase(0, 1);
    return s;
}

std::string iso_time(std::int64_t t) {
    const std::string s = strf("%Y-%m-%dT%H:%M:%S%z", t);   // ...+0530
    if (s.size() < 5) return s;
    // Appended piece by piece, not s.insert(): GCC 12 reports a false
    // -Wrestrict overlap on insert into a string (Debian 12 builds -Werror).
    std::string out;
    out.reserve(s.size() + 1);
    out.append(s, 0, s.size() - 2);
    out += ':';
    out.append(s, s.size() - 2, 2);
    return out;
}

std::string json_escape(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 2);
    for (char c : in) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned char>(c));
                out += buf;
            } else {
                out += c;
            }
        }
    }
    return out;
}

void Json::key(const char* k) {
    if (!first_.back()) s_ += ',';
    first_.back() = false;
    if (k) {
        // Appended in pieces: "literal" + std::string trips a false GCC 12
        // -Wrestrict, fatal under -Werror on Debian 12.
        s_ += '"';
        s_ += json_escape(k);
        s_ += "\":";
    }
}

Json& Json::begin_object(const char* k) {
    key(k);
    s_ += '{';
    first_.push_back(true);
    return *this;
}
Json& Json::end_object() {
    s_ += '}';
    first_.pop_back();
    return *this;
}
Json& Json::begin_array(const char* k) {
    key(k);
    s_ += '[';
    first_.push_back(true);
    return *this;
}
Json& Json::end_array() {
    s_ += ']';
    first_.pop_back();
    return *this;
}
Json& Json::field(const char* k, const std::string& v) {
    key(k);
    s_ += '"';
    s_ += json_escape(v);
    s_ += '"';
    return *this;
}
Json& Json::field(const char* k, std::uint64_t v) {
    key(k);
    s_ += std::to_string(v);
    return *this;
}
Json& Json::field(const char* k, std::int64_t v) {
    key(k);
    s_ += std::to_string(v);
    return *this;
}
Json& Json::field(const char* k, bool v) {
    key(k);
    s_ += v ? "true" : "false";
    return *this;
}
Json& Json::null(const char* k) {
    key(k);
    s_ += "null";
    return *this;
}
Json& Json::value(std::int64_t v) {
    key(nullptr);
    s_ += std::to_string(v);
    return *this;
}

Json& Json::value(const std::string& v) {
    key(nullptr);
    s_ += '"';
    s_ += json_escape(v);
    s_ += '"';
    return *this;
}

}  // namespace bandsight::cli
