// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bandsight::core {

// The daemon's live socket, read side only: a stream of frames, each a 4-byte
// little-endian length and a JSON body (daemon/json_frame.h). The GUI's Qt
// decoder is gui/frame_codec.cpp; this one is for readers without Qt.

// A body larger than this is protocol corruption (spec §4.1).
constexpr std::uint32_t kMaxFrameBytes = 1 << 20;

// Moves every whole frame body from the front of buf into out, leaving a partial
// tail in place. false on an implausible length: drop the connection.
bool extract_frames(std::string& buf, std::vector<std::string>& out);

struct HelloIface {
    int id = 0;
    std::string name;
    std::string kind;
};

struct Hello {
    std::string db_path;
    std::string version;   // empty when the daemon predates the field
    std::vector<HelloIface> ifaces;
};

// Bytes moved in one tick (one second), per measured interface. An interface
// that moved nothing is left out of the frame.
struct SampleIface {
    int id = 0;
    std::uint64_t rx = 0;
    std::uint64_t tx = 0;
};

struct Sample {
    std::int64_t ts = 0;
    std::vector<SampleIface> ifaces;
};

std::string frame_type(std::string_view body);   // the "t" field, "" if none
std::optional<Hello> decode_hello(std::string_view body);
std::optional<Sample> decode_sample(std::string_view body);

}  // namespace bandsight::core
