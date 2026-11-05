// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bandsight::cli {

// A named period: the windows the GUI offers, at the same resolutions, so
// "--period 24h" here and "24 hours" there read the same tier the same way.
struct NamedPeriod {
    const char* name;    // "24h"
    const char* label;   // "24 hours"
    int res;             // seconds per bucket
    std::int64_t range_s;
};
const std::vector<NamedPeriod>& named_periods();
const NamedPeriod* find_period(const std::string& name);

// What a command covers, resolved against a clock.
struct Period {
    enum class Kind { Window, Cycle, Range };
    Kind kind = Kind::Window;
    std::string name;    // "24h", "cycle" or "custom"
    std::string label;   // "last 24 hours", "this billing cycle", ...
    int res = 60;
    std::int64_t range_s = 0;   // Window only
    std::int64_t from = 0;      // shown to the reader; for a Window, now - range_s
    std::int64_t to = 0;
};

// The tier an explicit range reads: per-second rows are kept for an hour,
// minutes for 30 days, hours for a year, days after that (the daemon's
// retention). A range is read at the finest tier still holding its start.
int range_res(std::int64_t from, std::int64_t now);

// "2026-11-05", "2026-11-05T14:30", "2026-11-05 14:30:15": local time. A
// trailing "Z" or "+05:30" gives the zone explicitly. nullopt when unparsable.
std::optional<std::int64_t> parse_datetime(const std::string& s);

enum class Command { None, Status, Apps, Hosts, Types, Ifaces, Live, Top };
const char* command_name(Command c);

struct Options {
    Command command = Command::None;
    bool help = false;
    bool version = false;
    bool json = false;
    bool bytes = false;    // rates in B/s (MB/s) instead of bits (Mb/s)
    bool binary = false;   // volumes in GiB instead of GB
    std::string db_path;   // empty: ask the daemon, else the default
    std::string socket_path = "/run/bandsightd/sock";
    std::string period = "24h";
    std::string from, to;  // --from / --to, unparsed
    bool period_given = false;
    int limit = 10;        // 0: no limit
    std::string app;       // hosts --app
    bool all = false;      // ifaces --all
    double interval = 1;   // live --interval, seconds
    long count = 0;        // live --count, 0: until Ctrl-C
    bool ascii = false;    // top --ascii
};

constexpr const char* kDefaultDb = "/var/lib/bandsight/bandsight.db";

// Parses argv. On a usage error returns nullopt and sets *error to a message
// in plain words; the caller exits 2.
std::optional<Options> parse_args(const std::vector<std::string>& argv, std::string* error);

// Resolves o's period against now. db_cycle_day is the configured renewal day,
// for "cycle". nullopt with *error on an unusable --from/--to.
std::optional<Period> resolve_period(const Options& o, std::int64_t now, int cycle_day,
                                     std::string* error);

std::string help_text(Command c);   // Command::None: the overview
std::string version_text();

}  // namespace bandsight::cli
