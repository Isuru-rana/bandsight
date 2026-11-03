// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <sqlite3.h>

#include <cstdint>
#include <string>
#include <vector>

#include "dns_names.h"

namespace bandsight {

struct IfaceDelta {
    int ifindex = 0;
    std::uint64_t rx = 0;
    std::uint64_t tx = 0;
};

struct AppDelta {
    std::int64_t app_id = 0;
    int ifindex = 0;  // 0 = unknown egress interface
    std::uint64_t rx = 0;
    std::uint64_t tx = 0;
};

// Bytes one application exchanged with one peer over one interface. ip is the
// canonical text form (see format_peer); "" is the no-peer bucket.
struct HostDelta {
    std::int64_t app_id = 0;
    int ifindex = 0;
    std::string ip;
    std::uint64_t rx = 0;
    std::uint64_t tx = 0;
};

// Bytes of one service over one interface: proto 6 (TCP) or 17 (UDP), and the
// service port chosen by service_port() (traffic_type.h), 0 for "other".
struct TypeDelta {
    int ifindex = 0;
    int proto = 0;
    int port = 0;
    std::uint64_t rx = 0;
    std::uint64_t tx = 0;
};

// The ifaces row for traffic from sockets in another network namespace
// (containers). Like row 0 ("unknown"), it is created by migrate() and never
// names a real interface. Its kind is "virtual": those bytes also cross a real
// interface on the way out, where the interface totals already count them, and
// container-to-container traffic never leaves the machine at all.
constexpr int kIfaceOtherNetns = -1;

class Store {
public:
    // path may be ":memory:" for tests. Throws std::runtime_error on failure.
    explicit Store(const std::string& path);
    ~Store();
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;

    void migrate();
    int schema_version();

    std::int64_t upsert_app(const std::string& exe, const std::string& name,
                            const std::string& cgroup_hint);
    void upsert_iface(int ifindex, const std::string& name, const std::string& kind);

    // The config table (spec §5). The only writes the GUI can cause, and only
    // through the daemon's validated set_config command - the GUI's own database
    // handle is read-only and stays that way (spec §3).
    std::string get_config(const std::string& key, const std::string& fallback);
    void set_config(const std::string& key, const std::string& value);

    // Writes one res=1 bucket for ifaces/apps. Host and type deltas go to the
    // res=60 bucket containing ts: those rows are minute-grained (there is no
    // res=1 tier for them), and the current minute is written live so the GUI
    // need not wait for a rollup. The first type write also records ts as
    // config 'types_since', when Traffic type recording began. Single
    // transaction. Repeat writes for the
    // same ts accumulate, so a restart mid-second cannot lose or duplicate a row.
    void write_samples(std::int64_t ts, const std::vector<IfaceDelta>& ifaces,
                       const std::vector<AppDelta>& apps,
                       const std::vector<HostDelta>& hosts = {},
                       const std::vector<TypeDelta>& types = {});

    // Names hosts rows by address. Never creates a row: most lookups are for
    // addresses nothing ever connects to. One transaction; a row already
    // holding that name is not rewritten.
    void set_hostnames(const std::vector<IpName>& names);

    sqlite3* handle() { return db_; }

private:
    void exec(const std::string& sql);
    sqlite3* db_ = nullptr;
    // config 'types_since' is known to be set: the first type write stores
    // its second there (INSERT OR IGNORE), and later writes need not ask.
    bool types_since_set_ = false;
};

}  // namespace bandsight
