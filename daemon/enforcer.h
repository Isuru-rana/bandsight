// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace bandsight {

// Per-application blocking. Deliberately unimplemented in v1: this interface
// exists so blocking can be added without touching Collector or Store. The
// implementation will attach a cgroup/skb program to the cgroups of blocked
// applications only, so unblocked traffic never touches a datapath hook.
class Enforcer {
public:
    virtual ~Enforcer() = default;
    virtual bool block(std::uint64_t cgroup_id) = 0;
    virtual bool unblock(std::uint64_t cgroup_id) = 0;
    virtual bool is_blocked(std::uint64_t cgroup_id) const = 0;
};

}  // namespace bandsight
