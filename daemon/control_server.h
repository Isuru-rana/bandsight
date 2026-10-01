// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <functional>
#include <string>

namespace bandsight {

// Transport-agnostic control plane. UnixSocketControl is the only v1
// implementation; a DBusControl can be added beside it without the Collector,
// Store or Enforcer learning which transport is in use.
class ControlServer {
public:
    virtual ~ControlServer() = default;

    // Called for a validated-shape set_config command. Returns an empty string
    // to accept the write, or the reason to report back. The control layer owns
    // no policy and no storage: DaemonLoop supplies both, which keeps the
    // allowlist testable without a socket and keeps the socket free of any
    // knowledge that a database exists.
    using ConfigHandler =
        std::function<std::string(const std::string& key, const std::string& value)>;
    virtual void set_config_handler(ConfigHandler h) = 0;

    virtual bool start() = 0;

    // Sent to every client on connect.
    virtual void set_hello(std::string framed_hello) = 0;

    virtual void broadcast(const std::string& framed) = 0;

    // Only to clients that subscribed to `topic`.
    virtual void broadcast_to(const std::string& topic, const std::string& framed) = 0;

    // Services new connections and inbound commands. Non-blocking when
    // timeout_ms is 0.
    virtual void poll(int timeout_ms) = 0;
};

}  // namespace bandsight
