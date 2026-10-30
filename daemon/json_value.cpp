// SPDX-License-Identifier: GPL-3.0-or-later
#include "json_value.h"

#include <cstdlib>

namespace bandsight {

const JsonValue* JsonValue::get(std::string_view key) const {
    if (type != Type::Object) return nullptr;
    for (const auto& [k, v] : obj)
        if (k == key) return &v;
    return nullptr;
}

namespace {

constexpr int kMaxDepth = 64;

class Parser {
public:
    explicit Parser(std::string_view t) : t_(t) {}

    std::optional<JsonValue> document() {
        JsonValue v;
        if (!value(v, 0)) return std::nullopt;
        ws();
        if (i_ != t_.size()) return std::nullopt;
        return v;
    }

private:
    void ws() {
        while (i_ < t_.size() &&
               (t_[i_] == ' ' || t_[i_] == '\t' || t_[i_] == '\n' || t_[i_] == '\r'))
            ++i_;
    }

    bool literal(std::string_view word) {
        if (t_.substr(i_, word.size()) != word) return false;
        i_ += word.size();
        return true;
    }

    bool value(JsonValue& v, int depth) {
        if (depth >= kMaxDepth) return false;
        ws();
        if (i_ >= t_.size()) return false;
        switch (t_[i_]) {
            case '{': return object(v, depth);
            case '[': return array(v, depth);
            case '"': v.type = JsonValue::Type::String; return string(v.str);
            case 't': v.type = JsonValue::Type::Bool; v.b = true; return literal("true");
            case 'f': v.type = JsonValue::Type::Bool; return literal("false");
            case 'n': return literal("null");
            default: return number(v);
        }
    }

    bool object(JsonValue& v, int depth) {
        v.type = JsonValue::Type::Object;
        ++i_;
        ws();
        if (i_ < t_.size() && t_[i_] == '}') { ++i_; return true; }
        for (;;) {
            ws();
            std::string key;
            if (i_ >= t_.size() || t_[i_] != '"' || !string(key)) return false;
            ws();
            if (i_ >= t_.size() || t_[i_] != ':') return false;
            ++i_;
            JsonValue member;
            if (!value(member, depth + 1)) return false;
            v.obj.emplace_back(std::move(key), std::move(member));
            ws();
            if (i_ >= t_.size()) return false;
            if (t_[i_] == '}') { ++i_; return true; }
            if (t_[i_] != ',') return false;
            ++i_;
        }
    }

    bool array(JsonValue& v, int depth) {
        v.type = JsonValue::Type::Array;
        ++i_;
        ws();
        if (i_ < t_.size() && t_[i_] == ']') { ++i_; return true; }
        for (;;) {
            JsonValue item;
            if (!value(item, depth + 1)) return false;
            v.arr.push_back(std::move(item));
            ws();
            if (i_ >= t_.size()) return false;
            if (t_[i_] == ']') { ++i_; return true; }
            if (t_[i_] != ',') return false;
            ++i_;
        }
    }

    bool hex4(unsigned& out) {
        if (i_ + 4 > t_.size()) return false;
        out = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = t_[i_++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= unsigned(c - '0');
            else if (c >= 'a' && c <= 'f') out |= unsigned(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= unsigned(c - 'A' + 10);
            else return false;
        }
        return true;
    }

    static void utf8(std::string& s, unsigned cp) {
        if (cp < 0x80) {
            s += char(cp);
        } else if (cp < 0x800) {
            s += char(0xC0 | (cp >> 6));
            s += char(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            s += char(0xE0 | (cp >> 12));
            s += char(0x80 | ((cp >> 6) & 0x3F));
            s += char(0x80 | (cp & 0x3F));
        } else {
            s += char(0xF0 | (cp >> 18));
            s += char(0x80 | ((cp >> 12) & 0x3F));
            s += char(0x80 | ((cp >> 6) & 0x3F));
            s += char(0x80 | (cp & 0x3F));
        }
    }

    bool string(std::string& out) {
        ++i_;   // opening quote
        while (i_ < t_.size()) {
            const char c = t_[i_++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return false;
            if (c != '\\') { out += c; continue; }
            if (i_ >= t_.size()) return false;
            switch (t_[i_++]) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp = 0;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp < 0xDC00) {
                        unsigned lo = 0;
                        if (!literal("\\u") || !hex4(lo) || lo < 0xDC00 || lo > 0xDFFF)
                            return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        return false;   // a lone low surrogate
                    }
                    utf8(out, cp);
                    break;
                }
                default: return false;
            }
        }
        return false;   // unterminated
    }

    bool number(JsonValue& v) {
        const std::size_t start = i_;
        if (i_ < t_.size() && t_[i_] == '-') ++i_;
        while (i_ < t_.size() &&
               ((t_[i_] >= '0' && t_[i_] <= '9') || t_[i_] == '.' || t_[i_] == 'e' ||
                t_[i_] == 'E' || t_[i_] == '+' || t_[i_] == '-'))
            ++i_;
        if (i_ == start) return false;
        const std::string tok(t_.substr(start, i_ - start));
        char* end = nullptr;
        v.num = std::strtod(tok.c_str(), &end);
        v.type = JsonValue::Type::Number;
        return end == tok.c_str() + tok.size();
    }

    std::string_view t_;
    std::size_t i_ = 0;
};

}  // namespace

std::optional<JsonValue> parse_json(std::string_view text) {
    return Parser(text).document();
}

}  // namespace bandsight
