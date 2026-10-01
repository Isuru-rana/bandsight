// SPDX-License-Identifier: GPL-3.0-or-later
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_tracing.h>

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

// Egress interface for a socket, or 0 when the dst cache is cold.
static __always_inline __u32 sk_ifindex(struct sock *sk)
{
    struct dst_entry *dst = (struct dst_entry *)BPF_CORE_READ(sk, sk_dst_cache);
    if (!dst)
        return 0;
    struct net_device *dev = BPF_CORE_READ(dst, dev);
    if (!dev)
        return 0;
    return BPF_CORE_READ(dev, ifindex);
}

static __always_inline void account(struct sock *sk, __s64 bytes, int is_rx)
{
    if (bytes <= 0)  // negative = error, zero = nothing sent
        return;

    struct app_key k = {};
    k.cgroup_id = bpf_get_current_cgroup_id();
    k.tgid = bpf_get_current_pid_tgid() >> 32;
    k.ifindex = sk_ifindex(sk);

    struct app_val *v = bpf_map_lookup_elem(&map_app, &k);
    if (!v) {
        struct app_val init = {};
        bpf_map_update_elem(&map_app, &k, &init, BPF_NOEXIST);
        v = bpf_map_lookup_elem(&map_app, &k);
        if (!v)
            return;
    }

    if (is_rx) {
        __sync_fetch_and_add(&v->rx, (__u64)bytes);
        __sync_fetch_and_add(&v->rx_pkts, 1);
    } else {
        __sync_fetch_and_add(&v->tx, (__u64)bytes);
        __sync_fetch_and_add(&v->tx_pkts, 1);
    }
}

SEC("fexit/tcp_sendmsg")
int BPF_PROG(bs_tcp_sendmsg, struct sock *sk, struct msghdr *msg, size_t size, int ret)
{
    account(sk, ret, 0);
    return 0;
}

SEC("fentry/tcp_cleanup_rbuf")
int BPF_PROG(bs_tcp_recv, struct sock *sk, int copied)
{
    account(sk, copied, 1);
    return 0;
}

SEC("fexit/udp_sendmsg")
int BPF_PROG(bs_udp_sendmsg, struct sock *sk, struct msghdr *msg, size_t len, int ret)
{
    account(sk, ret, 0);
    return 0;
}

SEC("fexit/udpv6_sendmsg")
int BPF_PROG(bs_udpv6_sendmsg, struct sock *sk, struct msghdr *msg, size_t len, int ret)
{
    account(sk, ret, 0);
    return 0;
}

SEC("fentry/skb_consume_udp")
int BPF_PROG(bs_udp_recv, struct sock *sk, struct sk_buff *skb, int len)
{
    account(sk, len, 1);
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
