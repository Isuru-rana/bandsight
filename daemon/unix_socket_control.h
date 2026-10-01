// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "control_server.h"

namespace bandsight {

class UnixSocketControl : public ControlServer {
public:
    explicit UnixSocketControl(std::string socket_path);
    ~UnixSocketControl() override;

    void set_config_handler(ConfigHandler h) override { config_handler_ = std::move(h); }
    bool start() override;
    void set_hello(std::string framed_hello) override;
    void broadcast(const std::string& framed) override;
    void broadcast_to(const std::string& topic, const std::string& framed) override;
    void poll(int timeout_ms) override;

private:
    struct Client {
        std::set<std::string> topics;
        std::string inbuf;
    };

    void accept_clients();
    void handle_readable(int fd);
    void handle_command(int fd, const std::string& line);
    void drop(int fd);
    void send_framed(int fd, const std::string& framed);

    std::string path_;
    // Only a successful bind() makes this socket ours to remove. Set there, so a
    // second instance that failed to bind cannot unlink the running one's path.
    bool bound_ = false;
    int listen_fd_ = -1;
    int epoll_fd_ = -1;
    std::string hello_;
    std::unordered_map<int, Client> clients_;
    ConfigHandler config_handler_;
};

}  // namespace bandsight
