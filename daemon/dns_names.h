// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace bandsight {

// An address and the name it was looked up by. ip is in format_peer's text
// form, so it can be matched against hosts.ip as-is.
struct IpName {
    std::string ip;
    std::string name;
    bool operator==(const IpName&) const = default;
};

// One varlink message from systemd-resolved's monitor, without its NUL.
// A SubscribeQueryResults reply with state "success" names each A/AAAA answer
// after the query's question - the name the app asked for, not the CNAME
// target, which is a CDN's internal name. A DumpCache reply has no question,
// so each cached address is named after its own record. Anything else,
// malformed text included, yields nothing. Names are lowercase, no trailing
// dot, and only letters, digits, '-', '_' and dots; others are dropped.
std::vector<IpName> names_from_reply(std::string_view message);

// True for a varlink error reply ({"error": ...}).
bool is_varlink_error(std::string_view message);

}  // namespace bandsight
