// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <deque>
#include <string>

#include "core/frames.h"

namespace bandsight::cli {

// The daemon's live socket, READ ONLY: this connects, reads the hello and the
// sample frames the daemon broadcasts to every client, and never writes a byte -
// no command and no config can reach the daemon from here.
class DaemonSocket {
public:
    enum class State {
        Running,      // connected, hello received
        NotRunning,   // no socket, or nothing listening on it
        NoAccess,     // the socket exists but this user may not connect
        NoAnswer,     // connected, but no hello in time, or not a bandsight socket
    };
    enum class Next { Sample, Timeout, Closed, Interrupted, Corrupt };

    DaemonSocket() = default;
    ~DaemonSocket();
    DaemonSocket(const DaemonSocket&) = delete;
    DaemonSocket& operator=(const DaemonSocket&) = delete;

    // Connects and waits up to timeout_ms for the hello frame.
    State connect(const std::string& path, int timeout_ms);
    const core::Hello& hello() const { return hello_; }
    const std::string& error() const { return error_; }   // why, for NoAccess / NoAnswer
    // For poll(): readable when a frame may be waiting. -1 when not connected.
    int fd() const { return fd_; }

    // The next sample frame, waiting up to timeout_ms (-1: no limit). Other
    // frame types are skipped. Interrupted when a signal arrived (Ctrl-C).
    Next next_sample(int timeout_ms, core::Sample& out);

private:
    // Reads what is there, waiting up to timeout_ms. false with `why` set when
    // the stream ended or broke.
    Next fill(int timeout_ms);

    int fd_ = -1;
    std::string buf_;
    std::deque<std::string> frames_;
    core::Hello hello_;
    std::string error_;
};

}  // namespace bandsight::cli
