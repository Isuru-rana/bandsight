// SPDX-License-Identifier: GPL-3.0-or-later
#include "netlink_stats.h"

#include <linux/if_link.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <system_error>

namespace bandsight {
namespace {

struct FdGuard {
    int fd;
    explicit FdGuard(int f) : fd(f) {}
    ~FdGuard() { if (fd >= 0) ::close(fd); }
    FdGuard(const FdGuard&) = delete;
    FdGuard& operator=(const FdGuard&) = delete;
};

void throw_errno(const char* what) {
    throw std::system_error(errno, std::generic_category(), what);
}

struct LinkRequest {
    struct nlmsghdr hdr;
    struct ifinfomsg body;
};

}  // namespace

std::vector<IfaceStats> read_iface_stats() {
    FdGuard sock(::socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE));
    if (sock.fd < 0) throw_errno("netlink socket");

    LinkRequest req{};
    req.hdr.nlmsg_len = NLMSG_LENGTH(sizeof(req.body));
    req.hdr.nlmsg_type = RTM_GETLINK;
    req.hdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    req.hdr.nlmsg_seq = 1;
    req.body.ifi_family = AF_UNSPEC;

    if (::send(sock.fd, &req, req.hdr.nlmsg_len, 0) < 0) throw_errno("netlink send");

    std::vector<IfaceStats> out;
    // Kernel link dumps can exceed a page; 32 KiB comfortably holds one datagram.
    std::vector<char> buf(32768);
    bool done = false;

    while (!done) {
        // Not const: NLMSG_NEXT below mutates its length argument as it
        // walks messages within this datagram.
        ssize_t len = ::recv(sock.fd, buf.data(), buf.size(), 0);
        if (len < 0) {
            if (errno == EINTR) continue;
            throw_errno("netlink recv");
        }
        if (len == 0) break;

        for (auto* nh = reinterpret_cast<struct nlmsghdr*>(buf.data());
             NLMSG_OK(nh, static_cast<unsigned>(len)); nh = NLMSG_NEXT(nh, len)) {
            if (nh->nlmsg_type == NLMSG_DONE) { done = true; break; }
            if (nh->nlmsg_type == NLMSG_ERROR) throw std::runtime_error("netlink error reply");
            if (nh->nlmsg_type != RTM_NEWLINK) continue;

            const auto* ifi = static_cast<const struct ifinfomsg*>(NLMSG_DATA(nh));
            IfaceStats entry;
            entry.ifindex = ifi->ifi_index;

            int attr_len = static_cast<int>(nh->nlmsg_len) -
                           NLMSG_LENGTH(sizeof(struct ifinfomsg));
            const auto* rta = reinterpret_cast<const struct rtattr*>(
                reinterpret_cast<const char*>(ifi) + NLMSG_ALIGN(sizeof(*ifi)));

            for (; RTA_OK(rta, attr_len); rta = RTA_NEXT(rta, attr_len)) {
                if (rta->rta_type == IFLA_IFNAME) {
                    entry.name = static_cast<const char*>(RTA_DATA(rta));
                } else if (rta->rta_type == IFLA_STATS64 &&
                           RTA_PAYLOAD(rta) >= sizeof(struct rtnl_link_stats64)) {
                    struct rtnl_link_stats64 s{};
                    // Copy rather than cast: IFLA_STATS64 is only 4-byte aligned.
                    std::memcpy(&s, RTA_DATA(rta), sizeof(s));
                    entry.rx_bytes = s.rx_bytes;
                    entry.tx_bytes = s.tx_bytes;
                }
            }

            if (!entry.name.empty()) out.push_back(std::move(entry));
        }
    }

    return out;
}

}  // namespace bandsight
