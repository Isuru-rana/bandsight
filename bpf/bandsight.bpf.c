// SPDX-License-Identifier: GPL-3.0-or-later
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_endian.h>
#include <bpf/bpf_tracing.h>

#include "ifindex_pick.h"

char LICENSE[] SEC("license") = "GPL";

struct app_key {
    __u64 cgroup_id;
    __u32 tgid;
    __u32 ifindex;
};

struct app_val {
    __u64 rx;
    __u64 tx;
    __u64 rx_pkts;
    __u64 tx_pkts;
};

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 16384);
    __type(key, struct app_key);
    __type(value, struct app_val);
} map_app SEC(".maps");

// Per-socket bytes with the peer, for the Hosts column. Keyed by socket cookie
// (stable for the socket's life, never reused). Drained with lookup-and-delete
// every second like map_app, so identity fields set at create time are
// refreshed every second and no lifecycle hook is needed for bytes.
struct sock_key {
    __u64 cookie;
};

struct sock_val {
    __u64 cgroup_id;
    __u32 tgid;
    __u32 ifindex;
    __u8  family;
    __u8  proto;
    __u16 rport;      // host byte order
    __u8  raddr[16];  // IPv4 stored v4-mapped
    __u32 pad;
    __u64 rx;
    __u64 tx;
};

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 16384);
    __type(key, struct sock_key);
    __type(value, struct sock_val);
} map_sock SEC(".maps");

// Bytes that could not be recorded per-app because map_app was full. Unlike a
// full map_sock, this loses them from the Apps breakdown entirely; they still
// show in the interface totals.
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} app_dropped SEC(".maps");

// Bytes that could not be recorded per-socket because map_sock was full. The
// per-app totals are unaffected (map_app is updated first, independently); only
// the host breakdown is short by this much.
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} sock_dropped SEC(".maps");

// The daemon's network namespace (inode of /proc/self/ns/net), set by
// userspace before load. 0 = unknown: then no socket is filtered.
const volatile __u32 host_netns_inum = 0;

// The interface a socket's bytes are credited to; see bs_pick_ifindex() for the
// order of the sources. hint is what the caller knows about this packet in
// particular (UDP ingress interface, or loopback), 0 when nothing.
//
// ifindexes are per-namespace: a container's eth0 is commonly ifindex 2, the
// same number as the host's first NIC, so a socket in another namespace is
// reported as BS_IFINDEX_OTHER_NETNS (or loopback), never as a host interface.
static __always_inline __u32 sk_ifindex(struct sock *sk, __u32 hint)
{
    int other_netns = 0;
    if (host_netns_inum) {
        __u32 inum = BPF_CORE_READ(sk, __sk_common.skc_net.net, ns.inum);
        other_netns = inum != host_netns_inum;
    }

    __u32 dst_ifindex = 0;
    struct dst_entry *dst = (struct dst_entry *)BPF_CORE_READ(sk, sk_dst_cache);
    if (dst) {
        struct net_device *dev = BPF_CORE_READ(dst, dev);
        if (dev)
            dst_ifindex = BPF_CORE_READ(dev, ifindex);
    }

    int bound = BPF_CORE_READ(sk, __sk_common.skc_bound_dev_if);

    // sock.sk_rx_dst_ifindex exists from 5.16 (inet_sock.rx_dst_ifindex
    // before). On an older kernel this source is simply not used.
    int rx_dst = 0;
    if (bpf_core_field_exists(sk->sk_rx_dst_ifindex))
        rx_dst = BPF_CORE_READ(sk, sk_rx_dst_ifindex);

    return bs_pick_ifindex(other_netns, hint, dst_ifindex, bound > 0 ? bound : 0,
                           rx_dst > 0 ? rx_dst : 0);
}

// Loopback when an unconnected UDP datagram is addressed to 127/8 or ::1. Such a
// socket has no cached route, and without this its loopback traffic (a local
// DNS stub, a dev server) would be counted as traffic on an unknown interface.
// msg_name is a kernel copy of the address for sendto/sendmsg/io_uring.
static __always_inline __u32 udp_dest_hint(struct msghdr *msg)
{
    void *name = BPF_CORE_READ(msg, msg_name);
    int namelen = BPF_CORE_READ(msg, msg_namelen);
    if (!name)
        return 0;

    __u16 family = 0;
    if (bpf_probe_read_kernel(&family, sizeof(family), name))
        return 0;

    unsigned char addr[16] = {};
    if (family == 2 /* AF_INET */ && namelen >= (int)sizeof(struct sockaddr_in)) {
        struct sockaddr_in *sin = name;
        if (bpf_probe_read_kernel(addr, 4, &sin->sin_addr))
            return 0;
    } else if (family == 10 /* AF_INET6 */ && namelen >= (int)sizeof(struct sockaddr_in6)) {
        struct sockaddr_in6 *sin6 = name;
        if (bpf_probe_read_kernel(addr, 16, &sin6->sin6_addr))
            return 0;
    } else {
        return 0;
    }
    return bs_loopback_addr(family, addr) ? BS_LOOPBACK_IFINDEX : 0;
}

static __always_inline void account_sock(struct sock *sk, __u64 bytes, int is_rx,
                                         __u8 proto, __u64 cgroup_id, __u32 tgid,
                                         __u32 ifindex)
{
    struct sock_key k = {};
    k.cookie = bpf_get_socket_cookie(sk);

    struct sock_val *v = bpf_map_lookup_elem(&map_sock, &k);
    if (!v) {
        struct sock_val init = {};
        init.cgroup_id = cgroup_id;
        init.tgid = tgid;
        init.ifindex = ifindex;
        init.proto = proto;
        init.family = BPF_CORE_READ(sk, __sk_common.skc_family);
        init.rport = bpf_ntohs(BPF_CORE_READ(sk, __sk_common.skc_dport));
        if (init.family == 2 /* AF_INET */) {
            __u32 d4 = BPF_CORE_READ(sk, __sk_common.skc_daddr);
            init.raddr[10] = 0xff;
            init.raddr[11] = 0xff;
            __builtin_memcpy(&init.raddr[12], &d4, 4);
        } else if (init.family == 10 /* AF_INET6 */) {
            BPF_CORE_READ_INTO(&init.raddr, sk, __sk_common.skc_v6_daddr.in6_u.u6_addr8);
        }
        // EEXIST (another CPU created it first) is fine: the lookup finds it.
        // A full map is the other failure, and the lookup below then misses.
        bpf_map_update_elem(&map_sock, &k, &init, BPF_NOEXIST);
        v = bpf_map_lookup_elem(&map_sock, &k);
        if (!v) {
            __u32 zero = 0;
            __u64 *d = bpf_map_lookup_elem(&sock_dropped, &zero);
            if (d)
                __sync_fetch_and_add(d, bytes);
            return;
        }
    }
    if (is_rx)
        __sync_fetch_and_add(&v->rx, bytes);
    else
        __sync_fetch_and_add(&v->tx, bytes);
}

static __always_inline void account(struct sock *sk, __s64 bytes, int is_rx, __u8 proto,
                                    __u32 hint)
{
    if (bytes <= 0)  // negative = error, zero = nothing sent
        return;

    struct app_key k = {};
    k.cgroup_id = bpf_get_current_cgroup_id();
    k.tgid = bpf_get_current_pid_tgid() >> 32;
    k.ifindex = sk_ifindex(sk, hint);

    struct app_val *v = bpf_map_lookup_elem(&map_app, &k);
    if (!v) {
        struct app_val init = {};
        bpf_map_update_elem(&map_app, &k, &init, BPF_NOEXIST);
        v = bpf_map_lookup_elem(&map_app, &k);
    }
    if (v) {
        if (is_rx) {
            __sync_fetch_and_add(&v->rx, (__u64)bytes);
            __sync_fetch_and_add(&v->rx_pkts, 1);
        } else {
            __sync_fetch_and_add(&v->tx, (__u64)bytes);
            __sync_fetch_and_add(&v->tx_pkts, 1);
        }
    } else {
        __u32 zero = 0;
        __u64 *d = bpf_map_lookup_elem(&app_dropped, &zero);
        if (d)
            __sync_fetch_and_add(d, (__u64)bytes);
    }

    // After map_app, and independent of it: a full map_sock must never cost
    // the per-app totals a byte, and a full map_app must not cost the hosts.
    account_sock(sk, (__u64)bytes, is_rx, proto, k.cgroup_id, k.tgid, k.ifindex);
}

SEC("fexit/tcp_sendmsg")
int BPF_PROG(bs_tcp_sendmsg, struct sock *sk, struct msghdr *msg, size_t size, int ret)
{
    account(sk, ret, 0, 6, 0);
    return 0;
}

SEC("fentry/tcp_cleanup_rbuf")
int BPF_PROG(bs_tcp_recv, struct sock *sk, int copied)
{
    account(sk, copied, 1, 6, 0);
    return 0;
}

SEC("fexit/udp_sendmsg")
int BPF_PROG(bs_udp_sendmsg, struct sock *sk, struct msghdr *msg, size_t len, int ret)
{
    if (ret <= 0)
        return 0;
    // An AF_INET6 socket sending to a v4-mapped address goes through
    // udpv6_sendmsg, which calls udp_sendmsg for the actual send. Both fexit
    // programs then fire for one datagram; it is counted by bs_udpv6_sendmsg
    // only. udp_sendmsg is not reached for an AF_INET6 socket any other way.
    if (BPF_CORE_READ(sk, __sk_common.skc_family) == 10 /* AF_INET6 */)
        return 0;
    account(sk, ret, 0, 17, udp_dest_hint(msg));
    return 0;
}

SEC("fexit/udpv6_sendmsg")
int BPF_PROG(bs_udpv6_sendmsg, struct sock *sk, struct msghdr *msg, size_t len, int ret)
{
    if (ret <= 0)
        return 0;
    account(sk, ret, 0, 17, udp_dest_hint(msg));
    return 0;
}

SEC("fentry/skb_consume_udp")
int BPF_PROG(bs_udp_recv, struct sock *sk, struct sk_buff *skb, int len)
{
    // The interface this datagram arrived on: exact, and the only source for an
    // unconnected socket, which has no cached route. skb_iif rather than
    // skb->dev: the receive path may clear dev once the skb is queued.
    int iif = len > 0 ? BPF_CORE_READ(skb, skb_iif) : 0;
    account(sk, len, 1, 17, iif > 0 ? iif : 0);
    return 0;
}

#define BS_EXE_LEN 256

enum bs_event_type {
    BS_EVENT_EXEC = 1,
    BS_EVENT_EXIT = 2,
};

struct bs_proc_event {
    __u32 type;
    __u32 tgid;
    char exe[BS_EXE_LEN];
};

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 1 << 18);
} events SEC(".maps");

// Single counter of exec/exit events lost to a full ring buffer (e.g. a
// `make -j16` build generating exec()s faster than the 1 Hz drain can keep
// up). A dropped exec is harmless; a dropped exit leaks Resolver's cache
// entry for that tgid forever (see FOLLOWUP I3) - this is how userspace finds
// out that happened at all, since bpf_ringbuf_reserve() failing is otherwise
// silent.
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} events_dropped SEC(".maps");

static __always_inline void note_dropped_event(void)
{
    __u32 zero = 0;
    __u64 *drops = bpf_map_lookup_elem(&events_dropped, &zero);
    if (drops)
        __sync_fetch_and_add(drops, 1);
}

SEC("tracepoint/sched/sched_process_exec")
int bs_on_exec(struct trace_event_raw_sched_process_exec *ctx)
{
    struct bs_proc_event *e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
    if (!e) {
        note_dropped_event();
        return 0;
    }

    e->type = BS_EVENT_EXEC;
    e->tgid = bpf_get_current_pid_tgid() >> 32;

    // bpf_ringbuf_reserve does NOT zero the slot, and slots are reused. If the
    // read below fails, whatever a previous event left here would be submitted as
    // this process's path - plausibly a different executable entirely, which
    // userspace would cache and then misattribute bytes to. One byte-store makes
    // a failed read produce an empty string instead, and Resolver::note_exec
    // already ignores empty paths, so it falls through to the /proc fallback.
    e->exe[0] = '\0';

    // filename is a __data_loc field: the low 16 bits hold its offset from ctx.
    unsigned short off = ctx->__data_loc_filename & 0xFFFF;
    bpf_probe_read_kernel_str(e->exe, sizeof(e->exe), (void *)ctx + off);

    bpf_ringbuf_submit(e, 0);
    return 0;
}

SEC("tracepoint/sched/sched_process_exit")
int bs_on_exit(void *ctx)
{
    __u64 id = bpf_get_current_pid_tgid();
    __u32 tgid = id >> 32;
    __u32 pid = (__u32)id;
    if (tgid != pid)  // thread exit, not process exit
        return 0;

    struct bs_proc_event *e = bpf_ringbuf_reserve(&events, sizeof(*e), 0);
    if (!e) {
        note_dropped_event();
        return 0;
    }
    e->type = BS_EVENT_EXIT;
    e->tgid = tgid;
    e->exe[0] = '\0';
    bpf_ringbuf_submit(e, 0);
    return 0;
}
