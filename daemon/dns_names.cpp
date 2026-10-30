// SPDX-License-Identifier: GPL-3.0-or-later
#include "dns_names.h"

#include <arpa/inet.h>

#include <cctype>
#include <cstdint>

#include "json_value.h"

namespace bandsight {

namespace {

constexpr int kTypeA = 1;
constexpr int kTypeAAAA = 28;

// "" for a name that is not plain DNS characters. Anything can be looked up,
// and the result is shown as a label: a control or right-to-left override
// character would let a name disguise itself on screen. Real names are
// letters, digits, '-', '_' and dots; international ones arrive as punycode.
std::string normalise(std::string name) {
    if (!name.empty() && name.back() == '.') name.pop_back();
    for (char& c : name) {
        c = char(std::tolower(static_cast<unsigned char>(c)));
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
              c == '.'))
            return {};
    }
    return name;
}

// The address of an A/AAAA record, spelled as format_peer spells it. "" for
// any other record type, or an address of the wrong length or out of range.
std::string address_of(const JsonValue& rr) {
    const JsonValue* key = rr.get("key");
    const JsonValue* type = key ? key->get("type") : nullptr;
    const JsonValue* addr = rr.get("address");
    if (!type || !addr || addr->type != JsonValue::Type::Array) return {};

    const int t = int(type->num);
    const std::size_t want = t == kTypeA ? 4 : t == kTypeAAAA ? 16 : 0;
    if (want == 0 || addr->arr.size() != want) return {};

    std::uint8_t bytes[16] = {};
    for (std::size_t i = 0; i < want; ++i) {
        const double b = addr->arr[i].num;
        if (addr->arr[i].type != JsonValue::Type::Number || b < 0 || b > 255) return {};
        bytes[i] = std::uint8_t(b);
    }
    char buf[INET6_ADDRSTRLEN] = {};
    inet_ntop(want == 4 ? AF_INET : AF_INET6, bytes, buf, sizeof(buf));
    return buf;
}

std::string name_of(const JsonValue& rr) {
    const JsonValue* key = rr.get("key");
    const JsonValue* name = key ? key->get("name") : nullptr;
    return name ? name->str : std::string();
}

void add(std::vector<IpName>& out, const JsonValue& rr, const std::string& asked) {
    std::string ip = address_of(rr);
    if (ip.empty()) return;
    std::string name = normalise(asked.empty() ? name_of(rr) : asked);
    if (name.empty()) return;
    out.push_back({std::move(ip), std::move(name)});
}

}  // namespace

std::vector<IpName> names_from_reply(std::string_view message) {
    std::vector<IpName> out;
    const auto doc = parse_json(message);
    const JsonValue* params = doc ? doc->get("parameters") : nullptr;
    if (!params) return out;

    if (const JsonValue* dump = params->get("dump")) {
        for (const JsonValue& scope : dump->arr) {
            const JsonValue* cache = scope.get("cache");
            if (!cache) continue;
            for (const JsonValue& entry : cache->arr) {
                const JsonValue* rrs = entry.get("rrs");
                if (!rrs) continue;
                for (const JsonValue& r : rrs->arr)
                    if (const JsonValue* rr = r.get("rr")) add(out, *rr, {});
            }
        }
        return out;
    }

    const JsonValue* state = params->get("state");
    if (!state || state->str != "success") return out;
    std::string asked;
    if (const JsonValue* q = params->get("question"); q && !q->arr.empty())
        if (const JsonValue* n = q->arr[0].get("name")) asked = n->str;
    if (const JsonValue* answer = params->get("answer"))
        for (const JsonValue& a : answer->arr)
            if (const JsonValue* rr = a.get("rr")) add(out, *rr, asked);
    return out;
}

bool is_varlink_error(std::string_view message) {
    const auto doc = parse_json(message);
    return doc && doc->get("error") != nullptr;
}

}  // namespace bandsight
