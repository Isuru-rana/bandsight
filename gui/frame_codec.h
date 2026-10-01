// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QByteArray>
#include <optional>
#include <vector>
#include "frame_types.h"

namespace bandsight::gui {

// A frame body larger than this is protocol corruption; drop the connection
// rather than allocate (spec §4.1).
constexpr quint32 kMaxFrameBytes = 1 << 20;

// Consumes whole frames from `buf`, leaving any partial tail in place.
// Returns bodies in order; nullopt on a malformed length so the caller can
// drop the connection. Never throws.
std::optional<std::vector<QByteArray>> extract_frames(QByteArray& buf);

QString frame_type(const QByteArray& body);
std::optional<HelloInfo> decode_hello(const QByteArray& body);
std::optional<SampleFrame> decode_sample(const QByteArray& body);

// The daemon's reason for refusing a command. Empty when the frame carries none,
// which still reads as a failure to the caller - configResult's bool is what
// says whether it worked.
QString decode_error(const QByteArray& body);

}  // namespace bandsight::gui
