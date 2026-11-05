// SPDX-License-Identifier: GPL-3.0-or-later
#include "args.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <set>

#include "build_info.h"
#include "core/cycle.h"

namespace bandsight::cli {

const std::vector<NamedPeriod>& named_periods() {
    // The GUI's windows (gui/frame_types.h, window_spec) at their resolutions;
    // test_query_parity holds the two tables together.
    static const std::vector<NamedPeriod> p{
        {"5m", "5 minutes", 1, 300},
        {"30m", "30 minutes", 1, 1800},
        {"1h", "1 hour", 60, 3600},
        {"6h", "6 hours", 60, 6 * 3600},
        {"12h", "12 hours", 60, 12 * 3600},
        {"24h", "24 hours", 60, 86400},
        {"7d", "7 days", 3600, 7LL * 86400},
        {"30d", "30 days", 3600, 30LL * 86400},
    };
    return p;
}

const NamedPeriod* find_period(const std::string& name) {
    for (const NamedPeriod& p : named_periods())
        if (name == p.name) return &p;
    return nullptr;
}

int range_res(std::int64_t from, std::int64_t now) {
    // Each tier's retention less a margin, so a row about to be pruned is not
    // what a range starts on.
    if (from >= now - 3600 + 60) return 1;
    if (from >= now - 30 * 86400 + 3600) return 60;
    if (from >= now - 365 * 86400 + 86400) return 3600;
    return 86400;
}

namespace {

bool digits(const std::string& s, std::size_t at, std::size_t n) {
    if (at + n > s.size()) return false;
    for (std::size_t i = at; i < at + n; ++i)
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    return true;
}

int num(const std::string& s, std::size_t at, std::size_t n) {
    return std::atoi(s.substr(at, n).c_str());
}

}  // namespace

std::optional<std::int64_t> parse_datetime(const std::string& s) {
    // YYYY-MM-DD
    if (!digits(s, 0, 4) || s.size() < 10 || s[4] != '-' || !digits(s, 5, 2) || s[7] != '-' ||
        !digits(s, 8, 2))
        return std::nullopt;
    std::tm tm{};
    tm.tm_year = num(s, 0, 4) - 1900;
    tm.tm_mon = num(s, 5, 2) - 1;
    tm.tm_mday = num(s, 8, 2);
    std::size_t at = 10;
    if (at < s.size() && (s[at] == 'T' || s[at] == ' ')) {
        if (!digits(s, at + 1, 2) || at + 3 >= s.size() || s[at + 3] != ':' || !digits(s, at + 4, 2))
            return std::nullopt;
        tm.tm_hour = num(s, at + 1, 2);
        tm.tm_min = num(s, at + 4, 2);
        at += 6;
        if (at < s.size() && s[at] == ':') {
            if (!digits(s, at + 1, 2)) return std::nullopt;
            tm.tm_sec = num(s, at + 1, 2);
            at += 3;
        }
    }
    const int mon = tm.tm_mon + 1, day = tm.tm_mday;
    if (mon < 1 || mon > 12 || day < 1 || day > 31 || tm.tm_hour > 23 || tm.tm_min > 59 ||
        tm.tm_sec > 59)
        return std::nullopt;

    // An explicit zone, else local time.
    std::optional<int> offset_s;
    if (at < s.size()) {
        if (s[at] == 'Z' && at + 1 == s.size()) {
            offset_s = 0;
        } else if ((s[at] == '+' || s[at] == '-') && digits(s, at + 1, 2)) {
            std::size_t mm = at + 3;
            if (mm < s.size() && s[mm] == ':') ++mm;
            if (!digits(s, mm, 2) || mm + 2 != s.size()) return std::nullopt;
            const int off = num(s, at + 1, 2) * 3600 + num(s, mm, 2) * 60;
            offset_s = s[at] == '-' ? -off : off;
        } else {
            return std::nullopt;
        }
    }

    const std::tm asked = tm;
    std::int64_t t;
    if (offset_s) {
        t = std::int64_t(timegm(&tm)) - *offset_s;
    } else {
        tm.tm_isdst = -1;
        t = std::int64_t(std::mktime(&tm));
    }
    // 31 February normalises to March: refuse it rather than guess.
    if (tm.tm_mday != asked.tm_mday || tm.tm_mon != asked.tm_mon) return std::nullopt;
    return t;
}

const char* command_name(Command c) {
    switch (c) {
    case Command::Status: return "status";
    case Command::Apps: return "apps";
    case Command::Hosts: return "hosts";
    case Command::Types: return "types";
    case Command::Ifaces: return "ifaces";
    case Command::Live: return "live";
    case Command::None: break;
    }
    return "";
}

namespace {

std::optional<Command> command_from(const std::string& s) {
    for (Command c : {Command::Status, Command::Apps, Command::Hosts, Command::Types,
                      Command::Ifaces, Command::Live})
        if (s == command_name(c)) return c;
    return std::nullopt;
}

// Which command takes which option. Global ones are accepted by every command.
bool takes(Command c, const std::string& opt) {
    static const std::set<std::string> period{"--period", "--from", "--to"};
    switch (c) {
    case Command::Apps: return period.count(opt) || opt == "--limit";
    case Command::Hosts: return period.count(opt) || opt == "--limit" || opt == "--app";
    case Command::Types: return period.count(opt);
    case Command::Ifaces: return period.count(opt) || opt == "--all";
    case Command::Live: return opt == "--interval" || opt == "--count";
    case Command::Status:
    case Command::None: break;
    }
    return false;
}

bool needs_value(const std::string& opt) {
    static const std::set<std::string> v{"--db", "--socket", "--period", "--from", "--to",
                                         "--limit", "--app", "--interval", "--count"};
    return v.count(opt) > 0;
}

}  // namespace

std::optional<Options> parse_args(const std::vector<std::string>& argv, std::string* error) {
    Options o;
    const auto usage = [&](const std::string& msg) -> std::optional<Options> {
        const std::string where = o.command == Command::None
                                      ? std::string("bandsight-cli --help")
                                      : "bandsight-cli " + std::string(command_name(o.command)) + " --help";
        *error = msg + "\nRun '" + where + "' for usage.";
        return std::nullopt;
    };

    for (std::size_t i = 1; i < argv.size(); ++i) {
        std::string a = argv[i];
        std::string value;
        bool has_inline = false;
        if (a.rfind("--", 0) == 0) {
            if (const auto eq = a.find('='); eq != std::string::npos) {
                value = a.substr(eq + 1);
                a = a.substr(0, eq);
                has_inline = true;
            }
        }
        if (a == "-h" || a == "--help") { o.help = true; continue; }
        if (a == "-V" || a == "--version") { o.version = true; continue; }
        if (a == "--json") { o.json = true; continue; }
        if (a == "--bytes") { o.bytes = true; continue; }
        if (a == "--binary") { o.binary = true; continue; }
        if (a == "--all") {
            if (!takes(o.command, a)) return usage("'" + a + "' is only for 'ifaces'.");
            o.all = true;
            continue;
        }
        if (a.rfind("-", 0) == 0 && a.size() > 1) {
            if (!needs_value(a)) return usage("Unknown option '" + a + "'.");
            if (a != "--db" && a != "--socket" && !takes(o.command, a)) {
                if (o.command == Command::None)
                    return usage("'" + a + "' goes after a command, e.g. 'bandsight-cli apps " + a + " ...'.");
                return usage("'" + std::string(command_name(o.command)) + "' does not take '" + a + "'.");
            }
            if (!has_inline) {
                if (i + 1 >= argv.size()) return usage("'" + a + "' needs a value.");
                value = argv[++i];
            }
            if (value.empty()) return usage("'" + a + "' needs a value.");
            if (a == "--db") o.db_path = value;
            else if (a == "--socket") o.socket_path = value;
            else if (a == "--period") { o.period = value; o.period_given = true; }
            else if (a == "--from") o.from = value;
            else if (a == "--to") o.to = value;
            else if (a == "--app") o.app = value;
            else if (a == "--limit") {
                char* end = nullptr;
                const long n = std::strtol(value.c_str(), &end, 10);
                if (*end || n < 0 || n > 1000000)
                    return usage("'--limit' takes a whole number, 0 for no limit.");
                o.limit = int(n);
            } else if (a == "--interval") {
                char* end = nullptr;
                const double s = std::strtod(value.c_str(), &end);
                if (*end || !(s >= 1) || s > 3600 || s != std::floor(s))
                    return usage("'--interval' takes whole seconds, 1 to 3600.");
                o.interval = s;
            } else if (a == "--count") {
                char* end = nullptr;
                const long n = std::strtol(value.c_str(), &end, 10);
                if (*end || n < 1) return usage("'--count' takes a number of lines, 1 or more.");
                o.count = n;
            }
            continue;
        }
        if (o.command != Command::None)
            return usage("Unexpected argument '" + a + "'.");
        const auto c = command_from(a);
        if (!c) return usage("Unknown command '" + a + "'.");
        o.command = *c;
    }

    if (o.help || o.version) return o;
    if (o.command == Command::None) return usage("No command given.");
    if (o.period_given && (!o.from.empty() || !o.to.empty()))
        return usage("Give either '--period' or '--from'/'--to', not both.");
    if (!o.to.empty() && o.from.empty()) return usage("'--to' needs '--from'.");
    if (o.period_given && o.period != "cycle" && !find_period(o.period))
        return usage("Unknown period '" + o.period +
                     "'. Use 5m, 30m, 1h, 6h, 12h, 24h, 7d, 30d or cycle.");
    if (!o.from.empty() && !parse_datetime(o.from))
        return usage("Cannot read '--from " + o.from + "'. Use e.g. 2026-11-05 or 2026-11-05T14:30.");
    if (!o.to.empty() && !parse_datetime(o.to))
        return usage("Cannot read '--to " + o.to + "'. Use e.g. 2026-11-05 or 2026-11-05T14:30.");
    if (!o.from.empty() && !o.to.empty() && *parse_datetime(o.from) >= *parse_datetime(o.to))
        return usage("'--from' must be before '--to'.");
    return o;
}

std::optional<Period> resolve_period(const Options& o, std::int64_t now, int cycle_day,
                                     std::string* error) {
    Period p;
    if (!o.from.empty()) {
        p.kind = Period::Kind::Range;
        p.name = "custom";
        p.from = *parse_datetime(o.from);
        p.to = o.to.empty() ? now : *parse_datetime(o.to);
        if (p.from >= now) {
            *error = "'--from' is in the future.";
            return std::nullopt;
        }
        p.res = range_res(p.from, now);
        return p;
    }
    if (o.period == "cycle") {
        const core::Cycle c = core::cycle_for(now, cycle_day);
        p.kind = Period::Kind::Cycle;
        p.name = "cycle";
        p.label = "this billing cycle";
        p.from = c.start;
        p.to = now;
        p.res = range_res(p.from, now);
        return p;
    }
    const NamedPeriod* n = find_period(o.period);
    p.kind = Period::Kind::Window;
    p.name = n->name;
    p.label = std::string("last ") + n->label;
    p.res = n->res;
    p.range_s = n->range_s;
    p.from = now - n->range_s;
    p.to = now;
    return p;
}

std::string version_text() { return std::string("bandsight-cli ") + BANDSIGHT_VERSION + "\n"; }

std::string help_text(Command c) {
    const std::string period =
        "  --period P      5m, 30m, 1h, 6h, 12h, 24h (default), 7d, 30d, or cycle\n"
        "                  (the current billing cycle so far)\n"
        "  --from T        start of an explicit period, local time:\n"
        "                  2026-11-05, 2026-11-05T14:30 or \"2026-11-05 14:30:15\"\n"
        "  --to T          its end (default: now)\n";
    const std::string common =
        "\nCommon options:\n"
        "  --json          machine-readable output: one JSON object (live: one per\n"
        "                  line), raw byte counts, ISO 8601 times; the README lists\n"
        "                  the keys\n"
        "  --bytes         rates in bytes (MB/s) instead of bits (Mb/s)\n"
        "  --binary        volumes in binary units (GiB) instead of decimal (GB)\n"
        "  --db PATH       read this database instead of the daemon's\n"
        "                  (default /var/lib/bandsight/bandsight.db)\n"
        "  --socket PATH   the daemon's socket (default /run/bandsightd/sock)\n"
        "  -h, --help      show help\n";
    switch (c) {
    case Command::Status:
        return "Usage: bandsight-cli status [options]\n\n"
               "Whether bandsightd is running and its version, the database in use, the\n"
               "billing cycle (used, cap and projection) and the current rate.\n" + common;
    case Command::Apps:
        return "Usage: bandsight-cli apps [--period P | --from T [--to T]] [--limit N]\n\n"
               "Download and upload per application over a period, largest first.\n"
               "Counts the measured interfaces (wired, Wi-Fi) and sockets with no route.\n"
               "Unattributed: traffic the interfaces carried that no application owns\n"
               "(virtual machines, containers, kernel traffic such as WireGuard), shown\n"
               "when it is over 5% of the total and over 1 MB.\n\n" + period +
               "  --limit N       rows to list before folding the rest into \"N others\"\n"
               "                  (default 10, 0 for all)\n" + common;
    case Command::Hosts:
        return "Usage: bandsight-cli hosts [--period P | --from T [--to T]] [--app NAME] [--limit N]\n\n"
               "Download and upload per remote host, largest first, with its name when\n"
               "one was seen and whether it is on the local network (LAN) or not (WAN).\n\n" + period +
               "  --app NAME      only this application's hosts: the name 'apps' shows,\n"
               "                  the executable's file name, or its full path\n"
               "  --limit N       rows to list before folding the rest into \"N others\"\n"
               "                  (default 10, 0 for all)\n" + common;
    case Command::Types:
        return "Usage: bandsight-cli types [--period P | --from T [--to T]]\n\n"
               "Download and upload per kind of traffic (HTTPS, QUIC, DNS, SSH...), by the\n"
               "protocol and well-known port of each connection.\n\n" + period + common;
    case Command::Ifaces:
        return "Usage: bandsight-cli ifaces [--period P | --from T [--to T]] [--all]\n\n"
               "Download and upload per network interface. Only wired and Wi-Fi interfaces\n"
               "are measured toward totals: loopback, VPN and virtual interfaces carry\n"
               "traffic that is counted again on a physical one, or never leaves the\n"
               "machine.\n\n" + period +
               "  --all           also list the interfaces that are not measured\n" + common;
    case Command::Live:
        return "Usage: bandsight-cli live [--interval S] [--count N]\n\n"
               "Current download and upload rates from bandsightd, until Ctrl-C. Only\n"
               "reads from the daemon; never changes anything.\n\n"
               "  --interval S    seconds per line, averaged (default 1)\n"
               "  --count N       stop after N lines\n" + common;
    case Command::None: break;
    }
    return "Usage: bandsight-cli <command> [options]\n"
           "       bandsight-cli --version | --help | <command> --help\n\n"
           "Network usage recorded by bandsightd, for terminals and headless machines.\n"
           "History commands read the database and work while the daemon is stopped.\n\n"
           "Commands:\n"
           "  status          daemon, database, billing cycle and current rate\n"
           "  apps            usage per application\n"
           "  hosts           usage per remote host\n"
           "  types           usage per kind of traffic (HTTPS, DNS, ...)\n"
           "  ifaces          usage per network interface\n"
           "  live            current rates until Ctrl-C\n\n"
           "Periods (apps, hosts, types, ifaces):\n" + period +
           common +
           "  -V, --version   show the version\n\n"
           "Reading the database needs membership of the 'bandsight' group:\n"
           "  sudo usermod -aG bandsight $USER   (then log in again)\n\n"
           "Exit status: 0 success, 1 runtime error, 2 usage error.\n"
           "Colour and bars are used only on a terminal, and never when NO_COLOR is set.\n";
}

}  // namespace bandsight::cli
