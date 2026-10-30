// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "dns_names.h"

namespace bandsight {

constexpr const char* kResolvedMonitorPath = "/run/systemd/resolve/io.systemd.Resolve.Monitor";

// Learns which name each address was looked up by, from systemd-resolved's
// varlink monitor: a live subscription to every query result, plus one dump of
// the cache per connection so lookups made before the daemon started still
// count. Root only (resolved asks polkit otherwise), which bandsightd is.
//
// Everything is non-blocking and driven from the 1 Hz tick: the daemon is
// single-threaded, and a name is only a label, so nothing here may delay the
// byte counting. Without resolved, or on any error, it stays quiet apart from
// one log line per outage and retries every 30 s.
class ResolvedMonitor {
public:
    // max_read_per_poll bounds the bytes taken from each connection per poll:
    // any local user can cause lookups, and a burst of them must not hold up
    // the tick. Whatever is left stays in the socket for the next poll.
    explicit ResolvedMonitor(std::string socket_path = kResolvedMonitorPath,
                             std::size_t max_read_per_poll = 256 * 1024);
    ~ResolvedMonitor();
    ResolvedMonitor(const ResolvedMonitor&) = delete;
    ResolvedMonitor& operator=(const ResolvedMonitor&) = delete;

    // Connects when due, reads whatever is waiting, and returns the pairs seen
    // since the last call, in arrival order.
    std::vector<IpName> poll(std::int64_t now);

    // The subscription is up.
    bool connected() const { return sub_.fd >= 0; }

    // The most recent name for ip; "" when none is known.
    const std::string& name_for(const std::string& ip) const;

private:
    struct Conn {
        int fd = -1;
        std::string buf;
    };

    void connect_now(std::int64_t now);
    // Reads c dry, appending the pairs of each complete message to out. False
    // when the connection ended or misbehaved; reason says how.
    bool drain(Conn& c, std::vector<IpName>& out, std::string& reason, bool& got_reply);
    void close(Conn& c);
    void outage(std::int64_t now, const std::string& reason);
    void remember(const IpName& p);

    std::string path_;
    std::size_t max_read_;
    Conn sub_;    // SubscribeQueryResults, for as long as it lasts
    Conn dump_;   // DumpCache, closed after its one reply
    std::int64_t retry_at_ = 0;
    bool logged_outage_ = false;
    std::unordered_map<std::string, std::string> names_;
};

}  // namespace bandsight
