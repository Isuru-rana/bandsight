/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Which interface a socket's bytes are credited to, decided from values the
 * BPF program has already read out of the kernel. Pure, so the same code is
 * compiled into bandsight.bpf.c and into the host-side unit test.
 */
#ifndef BANDSIGHT_IFINDEX_PICK_H
#define BANDSIGHT_IFINDEX_PICK_H

/* lo is ifindex 1 in every network namespace (LOOPBACK_IFINDEX). */
#define BS_LOOPBACK_IFINDEX 1u
/*
 * A socket outside the daemon's network namespace (a container). Its ifindex
 * means nothing here, so it is reported as this sentinel and stored under its
 * own interface row rather than as "unknown" - see daemon_loop.cpp.
 */
#define BS_IFINDEX_OTHER_NETNS 0xFFFFFFFFu

/*
 * Arguments are 0 when not available:
 *   hint    the ingress interface of the packet (UDP receive), or loopback for a
 *           datagram sent to a loopback address
 *   dst     the device of the socket's cached route (sk_dst_cache)
 *   bound   SO_BINDTODEVICE / VRF binding (skc_bound_dev_if)
 *   rx_dst  the interface the socket last received on (sk_rx_dst_ifindex)
 *
 * The first source that knows wins. A route-less unconnected UDP socket (WebRTC,
 * WireGuard userspace, some QUIC) used to yield 0 even while its packets said
 * exactly where they came from.
 */
static inline unsigned int bs_pick_ifindex(int other_netns, unsigned int hint,
                                           unsigned int dst, unsigned int bound,
                                           unsigned int rx_dst)
{
    unsigned int v = hint ? hint : dst ? dst : bound ? bound : rx_dst;

    /* Inside a container, loopback is still loopback; anything else is an
     * interface number from another namespace and must not collide with ours. */
    if (other_netns)
        return v == BS_LOOPBACK_IFINDEX ? BS_LOOPBACK_IFINDEX : BS_IFINDEX_OTHER_NETNS;
    return v;
}

/*
 * True for 127.0.0.0/8, ::1 and ::ffff:127.0.0.0/104. addr holds 4 bytes for
 * family 2 (AF_INET) or 16 for family 10 (AF_INET6), in network order.
 */
static inline int bs_loopback_addr(unsigned short family, const unsigned char *addr)
{
    int i;

    if (family == 2)
        return addr[0] == 127;
    if (family != 10)
        return 0;
    for (i = 0; i < 10; i++)
        if (addr[i])
            return 0;
    if (addr[10] == 0xff && addr[11] == 0xff)
        return addr[12] == 127;
    if (addr[10] || addr[11] || addr[12] || addr[13] || addr[14])
        return 0;
    return addr[15] == 1;
}

#endif
