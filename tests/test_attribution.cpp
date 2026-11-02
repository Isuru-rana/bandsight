// SPDX-License-Identifier: GPL-3.0-or-later
// Interface attribution: the BPF program's pure decision functions, the
// daemon's handling of their output, and the GUI's Unattributed rule.
#include "../bpf/ifindex_pick.h"
#include "check.h"
#include "daemon_loop.h"
#include "store.h"
#include "unattributed.h"

using namespace bandsight;

static void pick_ifindex() {
    // The packet's own ingress interface beats the cached route.
    CHECK(bs_pick_ifindex(0, 3, 2, 0, 0) == 3);
    // Route-less socket: falls through to the binding, then the last rx interface.
    CHECK(bs_pick_ifindex(0, 0, 2, 5, 7) == 2);
    CHECK(bs_pick_ifindex(0, 0, 0, 5, 7) == 5);
    CHECK(bs_pick_ifindex(0, 0, 0, 0, 7) == 7);
    // Nothing known: still 0, the "unknown" row.
    CHECK(bs_pick_ifindex(0, 0, 0, 0, 0) == 0);
    // Another namespace: never a host ifindex, except loopback.
    CHECK(bs_pick_ifindex(1, 0, 2, 0, 0) == BS_IFINDEX_OTHER_NETNS);
    CHECK(bs_pick_ifindex(1, 0, 0, 0, 0) == BS_IFINDEX_OTHER_NETNS);
    CHECK(bs_pick_ifindex(1, BS_LOOPBACK_IFINDEX, 0, 0, 0) == BS_LOOPBACK_IFINDEX);
    CHECK(bs_pick_ifindex(1, 0, BS_LOOPBACK_IFINDEX, 0, 0) == BS_LOOPBACK_IFINDEX);
}

static void loopback_addr() {
    const unsigned char v4_lo[4] = {127, 0, 0, 53};
    const unsigned char v4_lan[4] = {192, 168, 1, 1};
    CHECK(bs_loopback_addr(2, v4_lo));
    CHECK(!bs_loopback_addr(2, v4_lan));

    unsigned char v6[16] = {};
    v6[15] = 1;                                   // ::1
    CHECK(bs_loopback_addr(10, v6));
    v6[15] = 2;                                   // ::2
    CHECK(!bs_loopback_addr(10, v6));
    unsigned char mapped[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 127, 0, 0, 1};
    CHECK(bs_loopback_addr(10, mapped));          // ::ffff:127.0.0.1
    mapped[12] = 10;
    CHECK(!bs_loopback_addr(10, mapped));         // ::ffff:10.0.0.1
    unsigned char link_local[16] = {0xfe, 0x80};
    link_local[15] = 1;
    CHECK(!bs_loopback_addr(10, link_local));     // fe80::1
    CHECK(!bs_loopback_addr(1 /* AF_UNIX */, v4_lo));
}

static void other_netns_row() {
    Store s(":memory:");
    s.migrate();
    DaemonLoop loop(s, "/sys/class/net");
    CHECK(loop.clampIfindexForTest(BS_IFINDEX_OTHER_NETNS) == kIfaceOtherNetns);
    CHECK(loop.clampIfindexForTest(0) == 0);
    CHECK(loop.clampIfindexForTest(424242) == 0);   // never observed

    // The row exists, so a write under it satisfies the foreign key.
    const std::int64_t app = s.upsert_app("/usr/bin/dockerd-child", "child", "");
    bool wrote = true;
    try {
        s.write_samples(1000, {}, {{app, kIfaceOtherNetns, 10, 20}},
                        {{app, kIfaceOtherNetns, "172.17.0.2", 10, 20}});
    } catch (...) {
        wrote = false;
    }
    CHECK(wrote);

    // migrate() is re-run on every start and must stay idempotent.
    bool again = true;
    try { s.migrate(); } catch (...) { again = false; }
    CHECK(again);
}

static void unattributed_rule() {
    using bandsight::gui::unattributed;
    // The reported case: 1.12 GB in / 168.6 MB out, apps ~150 MB / ~15 MB.
    auto u = unattributed(1'120'000'000, 168'600'000, 150'000'000, 15'000'000);
    CHECK(u.shown);
    CHECK(u.down == 970'000'000);
    CHECK(u.up == 153'600'000);

    // Under 5% of the headline: rounding, not a gap.
    CHECK(!unattributed(100'000'000, 0, 96'000'000, 0).shown);
    CHECK(unattributed(100'000'000, 0, 94'000'000, 0).shown);
    // Over 5% but under 1 MB: too small to be worth a row.
    CHECK(!unattributed(900'000, 0, 0, 0).shown);
    // Each direction clamps on its own: apps over the headline upward must not
    // cancel a real gap downward.
    u = unattributed(50'000'000, 1'000'000, 10'000'000, 5'000'000);
    CHECK(u.down == 40'000'000 && u.up == 0 && u.shown);
    // Nothing measured, nothing to show.
    CHECK(!unattributed(0, 0, 0, 0).shown);
    CHECK(!unattributed(0, 0, 5'000'000, 5'000'000).shown);
}

int main() {
    pick_ifindex();
    loopback_addr();
    other_netns_row();
    unattributed_rule();
    if (g_failures) std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    else std::printf("test_attribution: all checks passed\n");
    return g_failures ? 1 : 0;
}
