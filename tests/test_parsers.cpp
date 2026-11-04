// SPDX-License-Identifier: GPL-3.0-or-later
// Everything the daemon parses from other processes: systemd-resolved's
// varlink replies (json_value, dns_names) and control-socket commands
// (json_frame, config_policy). All of it is input the root daemon must not
// trust.
#include <cmath>
#include <cstdio>
#include <string>

#include "check.h"
#include "config_policy.h"
#include "dns_names.h"
#include "json_frame.h"
#include "json_value.h"

using namespace bandsight;

namespace {

void json_values() {
    auto v = parse_json(R"( {"a":[1,2.5,-3e2],"b":{"c":"x\ny"},"t":true,"f":false,"n":null} )");
    CHECK(v.has_value());
    if (v) {
        const JsonValue* a = v->get("a");
        CHECK(a && a->arr.size() == 3 && a->arr[1].num == 2.5 && a->arr[2].num == -300);
        CHECK(v->get("b") && v->get("b")->get("c") && v->get("b")->get("c")->str == "x\ny");
        CHECK(v->get("t")->b && !v->get("f")->b);
        CHECK(v->get("n")->type == JsonValue::Type::Null);
        CHECK(!v->get("missing"));
        CHECK(!a->get("a"));   // get() on a non-object
    }
    // Escapes, including a surrogate pair, become UTF-8.
    v = parse_json(R"("\u00e9\ud83d\ude00\/")");
    CHECK(v && v->str == "\xc3\xa9\xf0\x9f\x98\x80/");

    // Refused: syntax errors, trailing garbage, bad escapes, raw control
    // characters, lone surrogates, truncation.
    for (const char* bad : {"", "{", "[1,]", "{\"a\" 1}", "{} x", "\"\\q\"", "\"a\x01\"",
                            "\"\\ud800\"", "\"\\udc00\"", "\"abc", "tru", "nul", "-", "1e",
                            "{\"a\":}", "[1 2]"})
        CHECK(!parse_json(bad));

    // Nesting is capped, so a hostile reply cannot exhaust the stack.
    std::string deep(63, '[');
    deep += std::string(63, ']');
    CHECK(parse_json(deep));
    std::string too_deep(100000, '[');
    too_deep += std::string(100000, ']');
    CHECK(!parse_json(too_deep));
}

void resolved_replies() {
    // A live query result names its answers after the question, not the CNAME.
    const char* reply = R"({"parameters":{"state":"success",
        "question":[{"class":1,"type":1,"name":"www.Example.COM."}],
        "answer":[{"rr":{"key":{"class":1,"type":5,"name":"www.example.com"},"name":"cdn.net"}},
                  {"rr":{"key":{"class":1,"type":1,"name":"cdn.net"},"address":[93,184,216,34]}},
                  {"rr":{"key":{"class":1,"type":28,"name":"cdn.net"},
                         "address":[38,6,40,0,0,0,0,0,0,0,0,0,0,0,0,1]}}]}})";
    auto names = names_from_reply(reply);
    CHECK(names.size() == 2);
    if (names.size() == 2) {
        CHECK((names[0] == IpName{"93.184.216.34", "www.example.com"}));
        CHECK((names[1] == IpName{"2606:2800::1", "www.example.com"}));
    }

    // A cache dump names each address after its own record.
    names = names_from_reply(R"({"parameters":{"dump":[{"cache":[{"rrs":[
        {"rr":{"key":{"type":1,"name":"a.lan"},"address":[192,168,1,10]}}]}]}]}})");
    CHECK(names.size() == 1 && names[0].name == "a.lan" && names[0].ip == "192.168.1.10");

    // Nothing from failures, wrong shapes, or out-of-range values.
    auto none = [](const char* msg) { return names_from_reply(msg).empty(); };
    CHECK(none(R"({"parameters":{"state":"failure","answer":[]}})"));
    CHECK(none("garbage"));
    CHECK(none(R"({"parameters":{"state":"success","question":[{"name":"x"}],
        "answer":[{"rr":{"key":{"type":1},"address":[1,2,3]}}]}})"));          // short
    CHECK(none(R"({"parameters":{"state":"success","question":[{"name":"x"}],
        "answer":[{"rr":{"key":{"type":1},"address":[1,2,3,256]}}]}})"));      // > 255
    CHECK(none(R"({"parameters":{"state":"success","question":[{"name":"x"}],
        "answer":[{"rr":{"key":{"type":1e300},"address":[1,2,3,4]}}]}})"));   // huge type
    CHECK(none(R"({"parameters":{"state":"success","question":[{"name":"x"}],
        "answer":[{"rr":{"key":{"type":"1"},"address":[1,2,3,4]}}]}})"));     // not a number
    // A name that could disguise itself on screen is dropped.
    CHECK(none(R"({"parameters":{"state":"success","question":[{"name":"evil\u202ecom"}],
        "answer":[{"rr":{"key":{"type":1},"address":[1,2,3,4]}}]}})"));
    CHECK(none(R"({"parameters":{"state":"success","question":[{"name":"a b"}],
        "answer":[{"rr":{"key":{"type":1},"address":[1,2,3,4]}}]}})"));

    CHECK(is_varlink_error(R"({"error":"io.systemd.System"})"));
    CHECK(!is_varlink_error(R"({"parameters":{}})"));
    CHECK(!is_varlink_error("{"));
}

void command_fields() {
    const std::string cmd =
        R"({"cmd":"set_config","key":"cap_bytes", "value" : "5000000000"})";
    CHECK(json_string_field(cmd, "key") == "cap_bytes");
    CHECK(json_string_field(cmd, "value") == "5000000000");
    CHECK(json_string_field(cmd, "missing").empty());
    // A field name inside another one does not match.
    CHECK(json_string_field(R"({"mykey":"a","key":"b"})", "key") == "b");
    // Not a string, or unterminated: nothing, so the command is refused.
    CHECK(json_string_field(R"({"value":5})", "value").empty());
    CHECK(json_string_field(R"({"value":"5)", "value").empty());

    // Output side: every control character escaped, frames length-prefixed.
    CHECK(escape_json("a\"b\\c\n\x01") == "a\\\"b\\\\c\\n\\u0001");
    const std::string f = frame("abc");
    CHECK(f.size() == 7 && f[0] == 3 && f[1] == 0 && f.substr(4) == "abc");
    CHECK(encode_error("no \"way\"") == R"({"t":"error","msg":"no \"way\""})");
}

void config_rules() {
    CHECK(validate_config("cap_bytes", "0").empty());
    CHECK(validate_config("cap_bytes", "18446744073709551615").empty());   // u64 max
    CHECK(!validate_config("cap_bytes", "18446744073709551616").empty());  // one over
    CHECK(!validate_config("cap_bytes", "-1").empty());
    CHECK(!validate_config("cap_bytes", "").empty());
    CHECK(!validate_config("cap_bytes", "1e9").empty());
    CHECK(!validate_config("cap_bytes", " 5").empty());
    CHECK(validate_config("cycle_start_day", "1").empty());
    CHECK(validate_config("cycle_start_day", "28").empty());
    CHECK(!validate_config("cycle_start_day", "0").empty());
    CHECK(!validate_config("cycle_start_day", "29").empty());
    // The allowlist is the security boundary: nothing else is writable.
    for (const char* key : {"schema_version", "types_since", "", "CAP_BYTES"})
        CHECK(!validate_config(key, "3").empty());
}

}  // namespace

int main() {
    json_values();
    resolved_replies();
    command_fields();
    config_rules();
    if (g_failures) std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    else std::printf("test_parsers: all checks passed\n");
    return g_failures ? 1 : 0;
}
