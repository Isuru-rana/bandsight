// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace bandsight {

struct FrameIface {
    int id = 0;
    std::uint64_t rx = 0;
    std::uint64_t tx = 0;
};

struct FrameApp {
    std::int64_t id = 0;
    std::uint64_t rx = 0;
    std::uint64_t tx = 0;
    int iface = 0;
};

struct HelloIface {
    int id = 0;
    std::string name;
    std::string kind;
};

std::string escape_json(std::string_view in);

// 4-byte little-endian length prefix followed by body.
std::string frame(std::string_view body);

std::string encode_hello(const std::string& db_path,
                         const std::vector<HelloIface>& ifaces);
std::string encode_sample(std::int64_t ts, const std::vector<FrameIface>& ifaces,
                          const std::vector<FrameApp>& apps);
std::string encode_pong();
std::string encode_error(const std::string& msg);
std::string encode_ok();

// Reads one plain string field out of an inbound command line. Deliberately not
// a JSON parser (see handle_command): it accepts "field" : "value" with optional
// whitespace and returns an empty string for everything else, so a malformed
// command is refused rather than half-understood. Escapes are not decoded - no
// value the command set accepts contains one, and validate_config rejects
// anything that is not a bare integer anyway.
std::string json_string_field(const std::string& line, const std::string& field);

}  // namespace bandsight
