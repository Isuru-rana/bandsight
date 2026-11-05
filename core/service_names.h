// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>

namespace bandsight::core {

// What a (protocol, service port) pair from type_samples is called: proto 6 is
// TCP, 17 UDP; port 0 is the daemon's "other" bucket. Several pairs share a name
// (DNS over TCP and UDP, the mail ports).
//
// nullptr when the pair has no name, in which case it reads as port_label().
// The names are plain literals so the GUI can pass them through its translator
// unchanged (context "Services"); see gui/service_names.cpp.
const char* known_service_name(int proto, int port);

// The name, or port_label() when there is none.
std::string service_name(int proto, int port);

// "443/tcp", "53/udp".
std::string port_label(int proto, int port);

}  // namespace bandsight::core
