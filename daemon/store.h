// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <sqlite3.h>

#include <cstdint>
#include <string>
#include <vector>

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

    // Writes one res=1 bucket. Single transaction. Repeat writes for the same ts
    // accumulate, so a restart mid-second cannot lose or duplicate a row.
    void write_samples(std::int64_t ts, const std::vector<IfaceDelta>& ifaces,
                       const std::vector<AppDelta>& apps);

    sqlite3* handle() { return db_; }

private:
    void exec(const std::string& sql);
    sqlite3* db_ = nullptr;
};

}  // namespace bandsight
