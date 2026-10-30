// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bandsight {

// Just enough JSON to read systemd's varlink replies. The daemon otherwise only
// WRITES json (json_frame.h), and a library dependency would have to be added
// to every package recipe for the sake of a few dozen fields.
struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<JsonValue> arr;
    std::vector<std::pair<std::string, JsonValue>> obj;

    // nullptr when absent, or when this is not an object.
    const JsonValue* get(std::string_view key) const;
};

// nullopt on any syntax error, trailing garbage, or nesting deeper than 64:
// the text comes from another process, so a hostile nest must not be able to
// exhaust the stack.
std::optional<JsonValue> parse_json(std::string_view text);

}  // namespace bandsight
