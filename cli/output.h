// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace bandsight::cli {

// How the output may decorate itself: colour and unicode bars only on a
// terminal, and never when NO_COLOR is set (https://no-color.org).
struct Style {
    bool fancy = false;
    std::string bold(const std::string& s) const;
    std::string dim(const std::string& s) const;
    std::string down(const std::string& s) const;   // the GUI's download hue, as near as ANSI gets
    std::string up(const std::string& s) const;
};

// A text table: a header, rows, columns aligned. Columns after `left_cols` are
// right-aligned (the numbers). A muted row (an "N others" fold, Unattributed)
// is dimmed. With bars, each row ends in a bar of its down and up bytes,
// scaled to the largest row.
class Table {
public:
    Table(std::vector<std::string> header, std::size_t left_cols);
    void add(std::vector<std::string> cells, std::uint64_t down = 0, std::uint64_t up = 0,
             bool muted = false);
    bool empty() const { return rows_.empty(); }
    std::string render(const Style& st, bool bars) const;

private:
    struct Row {
        std::vector<std::string> cells;
        std::uint64_t down, up;
        bool muted;
    };
    std::vector<std::string> header_;
    std::size_t left_;
    std::vector<Row> rows_;
};

// Displayed width of UTF-8 text, in code points.
std::size_t text_width(const std::string& s);

// Local time, for people: "2026-11-05 14:30".
std::string local_minutes(std::int64_t t);
// "5 Nov 2026"
std::string local_date(std::int64_t t);
// ISO 8601 with the local offset, for JSON: "2026-11-05T14:30:00+05:30".
std::string iso_time(std::int64_t t);

// A tiny JSON writer: compact, keys in the order written. Every --json output
// is one object on one line, so the order of keys is part of what is stable.
class Json {
public:
    Json& begin_object(const char* key = nullptr);
    Json& end_object();
    Json& begin_array(const char* key = nullptr);
    Json& end_array();
    Json& field(const char* key, const std::string& v);
    Json& field(const char* key, const char* v) { return field(key, std::string(v)); }
    Json& field(const char* key, std::uint64_t v);
    Json& field(const char* key, std::int64_t v);
    Json& field(const char* key, int v) { return field(key, std::int64_t(v)); }
    Json& field(const char* key, bool v);
    Json& null(const char* key);
    Json& value(const std::string& v);   // array elements
    Json& value(std::int64_t v);
    const std::string& str() const { return s_; }

private:
    void key(const char* k);
    std::string s_;
    std::vector<bool> first_{true};
};

std::string json_escape(const std::string& s);

}  // namespace bandsight::cli
