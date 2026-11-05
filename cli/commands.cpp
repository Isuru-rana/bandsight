// SPDX-License-Identifier: GPL-3.0-or-later
#include "commands.h"

#include <algorithm>
#include <cctype>
#include <csignal>
#include <cstdio>
#include <map>
#include <optional>

#include "args.h"
#include "core/app_labels.h"
#include "core/history_db.h"
#include "core/host_zone.h"
#include "core/service_names.h"
#include "core/unattributed.h"
#include "core/units.h"
#include "daemon_socket.h"
#include "output.h"
#include "time_buckets.h"

namespace bandsight::cli {

namespace {

volatile std::sig_atomic_t g_stop = 0;

void out(const std::string& s) {
    std::fputs(s.c_str(), stdout);
}
void err(const std::string& s) {
    std::fputs(s.c_str(), stderr);
    if (s.empty() || s.back() != '\n') std::fputc('\n', stderr);
}

// Interface kinds that count toward totals (spec §2.2); the GUI's
// kind_is_measured().
bool measured_kind(const std::string& kind) { return kind == "physical" || kind == "wifi"; }

struct Ctx {
    const Options& o;
    const Env& env;
    Style st;
    core::RateUnit rate;
    core::VolumeUnit vol;

    std::string volume(std::uint64_t b) const { return core::format_volume(b, vol); }
    std::string rate_of(std::uint64_t b) const { return core::format_rate(b, rate); }
};

struct DbOpen {
    std::string path;
    core::HistoryDb db;
};

std::string db_error(const std::string& path, const core::HistoryDb& db) {
    const std::string head = "Cannot read " + path + ": ";
    switch (db.open_error()) {
    case core::OpenError::NotFound:
        return head + "the file does not exist. Is bandsightd installed and started? "
                      "(sudo systemctl enable --now bandsightd)";
    case core::OpenError::Permission:
        return head + "add yourself to the 'bandsight' group (sudo usermod -aG bandsight $USER), "
                      "then log in again.";
    case core::OpenError::NotADatabase:
        return head + "it is not a bandsight database.";
    case core::OpenError::Other:
    case core::OpenError::None:
        break;
    }
    return head + db.error() + ".";
}

// Which database to read: --db, else the one the running daemon names in its
// greeting, else the default. A history command still works with the daemon
// stopped, and says so once on stderr.
bool open_db(const Ctx& c, DbOpen& d, const DaemonSocket* daemon, DaemonSocket::State state,
             bool quiet_about_daemon) {
    if (!c.o.db_path.empty()) {
        d.path = c.o.db_path;
    } else {
        if (!daemon) {
            static DaemonSocket probe;
            state = probe.connect(c.o.socket_path, 1500);
            daemon = &probe;
        }
        if (state == DaemonSocket::State::Running && !daemon->hello().db_path.empty())
            d.path = daemon->hello().db_path;
        else
            d.path = kDefaultDb;
        if (!quiet_about_daemon) {
            switch (state) {
            case DaemonSocket::State::Running: break;
            case DaemonSocket::State::NotRunning:
                err("bandsightd is not running; showing history only.");
                break;
            case DaemonSocket::State::NoAccess:
                err("Cannot connect to bandsightd at " + c.o.socket_path + " (" + daemon->error() +
                    "); showing history only.");
                break;
            case DaemonSocket::State::NoAnswer:
                err("bandsightd did not answer at " + c.o.socket_path + " (" + daemon->error() +
                    "); showing history only.");
                break;
            }
        }
    }
    if (!d.db.open(d.path)) {
        err(db_error(d.path, d.db));
        return false;
    }
    return true;
}

int cycle_day(core::HistoryDb& db) {
    const auto v = db.config_value("cycle_start_day");
    const auto d = v ? core::parse_i64(*v) : std::nullopt;
    return d && *d >= 1 && *d <= 31 ? int(*d) : 1;
}

std::optional<Period> period_for(const Ctx& c, core::HistoryDb& db) {
    std::string why;
    auto p = resolve_period(c.o, c.env.now, cycle_day(db), &why);
    if (!p) err(why);
    return p;
}

// "Applications, last 24 hours (2026-11-04 12:00 to 2026-11-05 12:00)"
std::string heading(const std::string& what, const Period& p) {
    std::string h = what;
    if (!p.label.empty()) h += ", " + p.label;
    return h + " (" + local_minutes(p.from) + " to " + local_minutes(p.to) + ")\n\n";
}

void json_period(Json& j, const Period& p) {
    j.begin_object("period")
        .field("name", p.name)
        .field("from", iso_time(p.from))
        .field("to", iso_time(p.to))
        .field("resolution_s", p.res)
        .end_object();
}

void json_bytes(Json& j, std::uint64_t down, std::uint64_t up) {
    j.field("down_bytes", down).field("up_bytes", up).field("total_bytes", down + up);
}

// The totals a period's breakdowns are measured against: the measured interfaces.
struct Headline {
    bool known = false;
    std::uint64_t down = 0, up = 0;
};

struct Window {
    std::int64_t from, to;
};

// The bucket span apps and interfaces read: the GUI's grid for a named window,
// else the range floored to whole buckets.
Window bucket_window(const Period& p, std::int64_t now) {
    if (p.kind == Period::Kind::Window) {
        const core::Span s = core::window_span(p.res, p.range_s, now);
        return {s.from, s.to};
    }
    // The daemon's own bucket rule: res=3600 rows sit on local hours, which a
    // plain modulo misses in a zone whose offset is not whole hours.
    return {bandsight::bucket_start(p.from, p.res), p.to};
}

std::optional<std::vector<core::IfaceTotal>> iface_totals(core::HistoryDb& db, const Period& p,
                                                          std::int64_t now) {
    const Window w = bucket_window(p, now);
    return db.iface_totals(w.from, w.to, p.res);
}

Headline headline(const std::optional<std::vector<core::IfaceTotal>>& ifs) {
    Headline h;
    if (!ifs) return h;
    h.known = true;
    for (const auto& i : *ifs)
        if (measured_kind(i.kind)) {
            h.down += i.rx;
            h.up += i.tx;
        }
    return h;
}

std::string total_line(const Ctx& c, const Headline& h) {
    return "\nTotal on measured interfaces: " + c.volume(h.down) + " down, " + c.volume(h.up) +
           " up, " + c.volume(h.down + h.up) + " in all\n";
}

// A ranked row with its bytes; folded past the limit.
struct Ranked {
    std::vector<std::string> cells;
    std::uint64_t down = 0, up = 0;
};

struct Folded {
    std::size_t shown = 0;   // rows before the fold
    std::size_t others = 0;
    std::uint64_t down = 0, up = 0;
};

Folded fold(std::size_t n, int limit, const std::vector<std::uint64_t>& down,
            const std::vector<std::uint64_t>& up) {
    Folded f;
    f.shown = limit <= 0 || n <= std::size_t(limit) ? n : std::size_t(limit);
    for (std::size_t i = f.shown; i < n; ++i) {
        ++f.others;
        f.down += down[i];
        f.up += up[i];
    }
    return f;
}

std::string others_label(std::size_t n) {
    return std::to_string(n) + (n == 1 ? " other" : " others");
}

std::vector<std::string> byte_cells(const Ctx& c, std::uint64_t down, std::uint64_t up) {
    return {c.volume(down), c.volume(up), c.volume(down + up)};
}

// --- apps ----------------------------------------------------------------------

int cmd_apps(const Ctx& c) {
    DbOpen d;
    if (!open_db(c, d, nullptr, DaemonSocket::State::NotRunning, false)) return kExitRuntime;
    const auto p = period_for(c, d.db);
    if (!p) return kExitUsage;

    const Window w = bucket_window(*p, c.env.now);
    const auto apps = d.db.app_totals(w.from, w.to, p->res);
    if (!apps) {
        err("Cannot read application totals from " + d.path + ": " + d.db.last_query_error());
        return kExitRuntime;
    }
    const Headline h = headline(iface_totals(d.db, *p, c.env.now));

    std::vector<core::LabelledExe> in;
    std::vector<std::uint64_t> downs, ups;
    std::uint64_t all_down = 0, all_up = 0;
    for (const auto& a : *apps) {
        in.push_back({core::app_display_name(a.exe, a.name), a.exe});
        downs.push_back(a.rx);
        ups.push_back(a.tx);
        all_down += a.rx;
        all_up += a.tx;
    }
    const auto labels = core::disambiguated_labels(in);
    const Folded f = fold(apps->size(), c.o.limit, downs, ups);
    const core::Unattributed un = h.known ? core::unattributed(h.down, h.up, all_down, all_up)
                                          : core::Unattributed{};

    if (c.o.json) {
        Json j;
        j.begin_object().field("command", "apps");
        json_period(j, *p);
        if (h.known) {
            j.begin_object("total");
            json_bytes(j, h.down, h.up);
            j.end_object();
        } else {
            j.null("total");
        }
        j.begin_array("apps");
        for (std::size_t i = 0; i < f.shown; ++i) {
            const auto& a = (*apps)[i];
            j.begin_object().field("label", labels[i]).field("name", a.name).field("exe", a.exe);
            json_bytes(j, a.rx, a.tx);
            j.end_object();
        }
        j.end_array();
        if (f.others) {
            j.begin_object("others").field("count", std::uint64_t(f.others));
            json_bytes(j, f.down, f.up);
            j.end_object();
        } else {
            j.null("others");
        }
        if (un.shown) {
            j.begin_object("unattributed");
            json_bytes(j, un.down, un.up);
            j.end_object();
        } else {
            j.null("unattributed");
        }
        j.end_object();
        out(j.str() + "\n");
        return kExitOk;
    }

    std::string s = heading("Applications", *p);
    if (apps->empty()) {
        s += "No application traffic in this period.\n";
    } else {
        Table t({"APPLICATION", "DOWN", "UP", "TOTAL"}, 1);
        for (std::size_t i = 0; i < f.shown; ++i) {
            auto cells = byte_cells(c, (*apps)[i].rx, (*apps)[i].tx);
            cells.insert(cells.begin(), labels[i]);
            t.add(cells, (*apps)[i].rx, (*apps)[i].tx);
        }
        if (f.others) {
            auto cells = byte_cells(c, f.down, f.up);
            cells.insert(cells.begin(), others_label(f.others));
            t.add(cells, f.down, f.up, true);
        }
        if (un.shown) {
            auto cells = byte_cells(c, un.down, un.up);
            cells.insert(cells.begin(), "Unattributed");
            t.add(cells, un.down, un.up, true);
        }
        s += t.render(c.st, c.st.fancy);
    }
    if (h.known) s += total_line(c, h);
    if (un.shown)
        s += "Unattributed: counted on the interfaces but not credited to any application\n"
             "(virtual machines, containers, kernel traffic such as WireGuard).\n";
    out(s);
    return kExitOk;
}

// --- hosts ---------------------------------------------------------------------

std::string lower(std::string s) {
    for (char& ch : s) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

bool app_matches(const std::string& want, const core::AppHostTotal& p) {
    const std::string w = lower(want);
    const std::string base = p.exe.substr(p.exe.rfind('/') + 1);
    return w == lower(core::app_display_name(p.exe, p.app_name)) || w == lower(base) ||
           want == p.exe;
}

const char* zone_key(core::HostZone z) {
    switch (z) {
    case core::HostZone::Lan: return "lan";
    case core::HostZone::Wan: return "wan";
    case core::HostZone::None: break;
    }
    return "none";
}

int cmd_hosts(const Ctx& c) {
    DbOpen d;
    if (!open_db(c, d, nullptr, DaemonSocket::State::NotRunning, false)) return kExitRuntime;
    const auto p = period_for(c, d.db);
    if (!p) return kExitUsage;

    const core::HostPairs r = p->kind == Period::Kind::Window
                                  ? d.db.host_pairs_window(p->res, p->range_s, c.env.now)
                                  : d.db.host_pairs_range(p->from, p->to, p->res, c.env.now);
    if (!r.ok) {
        err("Cannot read host totals from " + d.path + ": " + d.db.last_query_error());
        return kExitRuntime;
    }
    const bool filtered = !c.o.app.empty();
    const Headline h = filtered ? Headline{} : headline(iface_totals(d.db, *p, c.env.now));

    // Pairs -> one row per host, like the GUI's Hosts column.
    struct Acc {
        std::string ip, hostname;
        std::uint64_t rx = 0, tx = 0;
        std::map<std::string, std::pair<std::string, std::uint64_t>> apps;   // exe -> label, bytes
    };
    std::map<std::string, Acc> by_key;
    std::uint64_t pair_down = 0, pair_up = 0;
    bool any_app = false;
    for (const auto& pr : r.pairs) {
        pair_down += pr.rx;
        pair_up += pr.tx;
        if (filtered && !app_matches(c.o.app, pr)) continue;
        any_app = true;
        Acc& a = by_key[pr.ip.empty() ? "(no-peer)" : pr.ip];
        a.ip = pr.ip;
        a.hostname = pr.hostname;
        a.rx += pr.rx;
        a.tx += pr.tx;
        auto& app = a.apps[pr.exe];
        app.first = core::app_display_name(pr.exe, pr.app_name);
        app.second += pr.rx + pr.tx;
    }
    std::vector<const Acc*> rows;
    for (const auto& [k, a] : by_key) rows.push_back(&a);
    std::stable_sort(rows.begin(), rows.end(),
                     [](const Acc* x, const Acc* y) { return x->rx + x->tx > y->rx + y->tx; });
    std::vector<std::uint64_t> downs, ups;
    for (const Acc* a : rows) {
        downs.push_back(a->rx);
        ups.push_back(a->tx);
    }
    const Folded f = fold(rows.size(), c.o.limit, downs, ups);
    // Not when host recording began inside the period, or there is none: the
    // shortfall is then expected.
    const core::Unattributed un =
        h.known && r.collected && r.since <= 0 && !r.pairs.empty()
            ? core::unattributed(h.down, h.up, pair_down, pair_up)
            : core::Unattributed{};
    const auto label = [](const Acc& a) {
        return !a.hostname.empty() ? a.hostname : a.ip.empty() ? std::string("No peer address") : a.ip;
    };
    // Who talked to a host, largest first: what the GUI's tooltip lists.
    struct HostApp {
        std::string exe, label;
        std::uint64_t bytes;
    };
    const auto top_apps = [](const Acc& a) {
        std::vector<HostApp> v;
        for (const auto& [exe, app] : a.apps) v.push_back({exe, app.first, app.second});
        std::stable_sort(v.begin(), v.end(),
                         [](const HostApp& x, const HostApp& y) { return x.bytes > y.bytes; });
        return v;
    };

    if (c.o.json) {
        Json j;
        j.begin_object().field("command", "hosts");
        json_period(j, *p);
        if (filtered) j.field("app", c.o.app); else j.null("app");
        j.field("collected", r.collected);
        if (r.since > 0) j.field("recorded_since", iso_time(r.since)); else j.null("recorded_since");
        if (h.known) {
            j.begin_object("total");
            json_bytes(j, h.down, h.up);
            j.end_object();
        } else {
            j.null("total");
        }
        j.begin_array("hosts");
        for (std::size_t i = 0; i < f.shown; ++i) {
            const Acc& a = *rows[i];
            j.begin_object().field("label", label(a));
            if (a.ip.empty()) j.null("ip"); else j.field("ip", a.ip);
            if (a.hostname.empty()) j.null("hostname"); else j.field("hostname", a.hostname);
            j.field("zone", zone_key(core::host_zone(a.ip)));
            json_bytes(j, a.rx, a.tx);
            j.begin_array("apps");
            for (const HostApp& h : top_apps(a))
                j.begin_object().field("label", h.label).field("exe", h.exe)
                    .field("total_bytes", h.bytes).end_object();
            j.end_array().end_object();
        }
        j.end_array();
        if (f.others) {
            j.begin_object("others").field("count", std::uint64_t(f.others));
            json_bytes(j, f.down, f.up);
            j.end_object();
        } else {
            j.null("others");
        }
        if (un.shown) {
            j.begin_object("unattributed");
            json_bytes(j, un.down, un.up);
            j.end_object();
        } else {
            j.null("unattributed");
        }
        j.end_object();
        out(j.str() + "\n");
        return kExitOk;
    }

    std::string s = heading(filtered ? "Hosts for " + c.o.app : std::string("Hosts"), *p);
    if (!r.collected) {
        s += "This database has no host breakdown. Update bandsightd to collect hosts.\n";
        out(s);
        return kExitOk;
    }
    if (rows.empty()) {
        s += filtered && !any_app ? "No host traffic for '" + c.o.app + "' in this period.\n"
                                  : "No host traffic in this period.\n";
    } else {
        Table t({"HOST", "ADDRESS", "NET", "DOWN", "UP", "TOTAL"}, 3);
        for (std::size_t i = 0; i < f.shown; ++i) {
            const Acc& a = *rows[i];
            const core::HostZone z = core::host_zone(a.ip);
            auto cells = byte_cells(c, a.rx, a.tx);
            cells.insert(cells.begin(), {label(a), a.hostname.empty() ? "" : a.ip,
                                         z == core::HostZone::Lan ? "LAN"
                                         : z == core::HostZone::Wan ? "WAN" : "-"});
            t.add(cells, a.rx, a.tx);
        }
        if (f.others) {
            auto cells = byte_cells(c, f.down, f.up);
            cells.insert(cells.begin(), {others_label(f.others), "", ""});
            t.add(cells, f.down, f.up, true);
        }
        if (un.shown) {
            auto cells = byte_cells(c, un.down, un.up);
            cells.insert(cells.begin(), {"Unattributed", "", ""});
            t.add(cells, un.down, un.up, true);
        }
        s += t.render(c.st, c.st.fancy);
    }
    if (h.known) s += total_line(c, h);
    if (r.since > 0)
        s += "Note: hosts are recorded since " + local_minutes(r.since) +
             "; earlier traffic has no host breakdown.\n";
    out(s);
    return kExitOk;
}

// --- types ---------------------------------------------------------------------

int cmd_types(const Ctx& c) {
    DbOpen d;
    if (!open_db(c, d, nullptr, DaemonSocket::State::NotRunning, false)) return kExitRuntime;
    const auto p = period_for(c, d.db);
    if (!p) return kExitUsage;

    const core::TypeTotals r = p->kind == Period::Kind::Window
                                   ? d.db.type_totals_window(p->res, p->range_s, c.env.now)
                                   : d.db.type_totals_range(p->from, p->to, p->res, c.env.now);
    if (!r.ok) {
        err("Traffic types could not be read from " + d.path + ": " + d.db.last_query_error());
        return kExitRuntime;
    }

    // Several (proto, port) pairs share a name: DNS over TCP and UDP, the mail
    // ports. One row per name, its pairs listed by size.
    struct Port {
        int proto, port;
        std::uint64_t rx, tx;
    };
    struct Acc {
        std::string name;
        std::uint64_t rx = 0, tx = 0;
        std::vector<Port> ports;
    };
    std::map<std::string, Acc> by_name;
    for (const auto& t : r.totals) {
        const std::string name = core::service_name(t.proto, t.port);
        Acc& a = by_name[name];
        a.name = name;
        a.rx += t.rx;
        a.tx += t.tx;
        if (t.port != 0) a.ports.push_back({t.proto, t.port, t.rx, t.tx});
    }
    std::vector<Acc> rows;
    for (auto& [k, a] : by_name) {
        std::stable_sort(a.ports.begin(), a.ports.end(), [](const Port& x, const Port& y) {
            return x.rx + x.tx > y.rx + y.tx;
        });
        rows.push_back(a);
    }
    std::stable_sort(rows.begin(), rows.end(), [](const Acc& x, const Acc& y) {
        return x.rx + x.tx != y.rx + y.tx ? x.rx + x.tx > y.rx + y.tx : x.name < y.name;
    });

    if (c.o.json) {
        Json j;
        j.begin_object().field("command", "types");
        json_period(j, *p);
        j.field("collected", r.collected);
        if (r.since > 0) j.field("recorded_since", iso_time(r.since)); else j.null("recorded_since");
        j.begin_array("types");
        for (const Acc& a : rows) {
            j.begin_object().field("service", a.name);
            j.begin_array("ports");
            for (const Port& pt : a.ports) {
                j.begin_object().field("proto", pt.proto == 17 ? "udp" : "tcp").field("port", pt.port);
                json_bytes(j, pt.rx, pt.tx);
                j.end_object();
            }
            j.end_array();
            json_bytes(j, a.rx, a.tx);
            j.end_object();
        }
        j.end_array().end_object();
        out(j.str() + "\n");
        return kExitOk;
    }

    std::string s = heading("Traffic types", *p);
    if (!r.collected) {
        s += "This database has no traffic types. Update bandsightd to see traffic types.\n";
        out(s);
        return kExitOk;
    }
    if (rows.empty()) {
        s += "No traffic in this period.\n";
    } else {
        Table t({"SERVICE", "PORTS", "DOWN", "UP", "TOTAL"}, 2);
        for (const Acc& a : rows) {
            std::string ports;
            for (std::size_t i = 0; i < a.ports.size(); ++i) {
                if (i) ports += ", ";
                ports += core::port_label(a.ports[i].proto, a.ports[i].port);
            }
            auto cells = byte_cells(c, a.rx, a.tx);
            cells.insert(cells.begin(), {a.name, ports});
            t.add(cells, a.rx, a.tx);
        }
        s += t.render(c.st, c.st.fancy);
    }
    if (r.since > 0)
        s += "\nNote: traffic types are recorded since " + local_minutes(r.since) +
             "; earlier traffic has no type.\n";
    out(s);
    return kExitOk;
}

// --- ifaces --------------------------------------------------------------------

int cmd_ifaces(const Ctx& c) {
    DbOpen d;
    if (!open_db(c, d, nullptr, DaemonSocket::State::NotRunning, false)) return kExitRuntime;
    const auto p = period_for(c, d.db);
    if (!p) return kExitUsage;

    const auto ifs = iface_totals(d.db, *p, c.env.now);
    if (!ifs) {
        err("Cannot read interface totals from " + d.path + ": " + d.db.last_query_error());
        return kExitRuntime;
    }
    const Headline h = headline(ifs);
    std::size_t hidden = 0;
    std::vector<const core::IfaceTotal*> rows;
    for (const auto& i : *ifs) {
        if (c.o.all || measured_kind(i.kind)) rows.push_back(&i);
        else ++hidden;
    }

    if (c.o.json) {
        Json j;
        j.begin_object().field("command", "ifaces");
        json_period(j, *p);
        j.begin_object("total");
        json_bytes(j, h.down, h.up);
        j.end_object();
        j.begin_array("interfaces");
        for (const auto* i : rows) {
            j.begin_object().field("name", i->name).field("kind", i->kind)
                .field("measured", measured_kind(i->kind));
            j.begin_array("ids");
            for (int id : i->ids) j.value(std::int64_t(id));
            j.end_array();
            json_bytes(j, i->rx, i->tx);
            j.end_object();
        }
        j.end_array().end_object();
        out(j.str() + "\n");
        return kExitOk;
    }

    std::string s = heading("Interfaces", *p);
    if (rows.empty()) {
        s += "No measured interfaces recorded yet.\n";
    } else {
        Table t({"INTERFACE", "KIND", "MEASURED", "DOWN", "UP", "TOTAL"}, 3);
        for (const auto* i : rows) {
            auto cells = byte_cells(c, i->rx, i->tx);
            cells.insert(cells.begin(), {i->name, i->kind, measured_kind(i->kind) ? "yes" : "no"});
            t.add(cells, i->rx, i->tx, !measured_kind(i->kind));
        }
        s += t.render(c.st, c.st.fancy);
    }
    s += total_line(c, h);
    if (hidden)
        s += std::to_string(hidden) + (hidden == 1 ? " interface" : " interfaces") +
             " not measured (loopback, VPN, virtual); --all lists them.\n";
    out(s);
    return kExitOk;
}

// --- status --------------------------------------------------------------------

const char* state_key(DaemonSocket::State s) {
    switch (s) {
    case DaemonSocket::State::Running: return "running";
    case DaemonSocket::State::NotRunning: return "not_running";
    case DaemonSocket::State::NoAccess: return "no_access";
    case DaemonSocket::State::NoAnswer: return "no_answer";
    }
    return "";
}

int cmd_status(const Ctx& c) {
    DaemonSocket daemon;
    const DaemonSocket::State state = daemon.connect(c.o.socket_path, 2000);
    const bool running = state == DaemonSocket::State::Running;

    DbOpen d;
    if (!open_db(c, d, &daemon, state, true)) return kExitRuntime;
    const core::Quota q = d.db.quota(c.env.now);

    std::optional<core::Sample> live;
    if (running) {
        core::Sample s;
        if (daemon.next_sample(2500, s) == DaemonSocket::Next::Sample) live = s;
    }
    std::uint64_t live_down = 0, live_up = 0;
    if (live)
        for (const auto& i : live->ifaces) {
            live_down += i.rx;
            live_up += i.tx;
        }

    if (c.o.json) {
        Json j;
        j.begin_object().field("command", "status");
        j.begin_object("daemon").field("running", running).field("state", state_key(state));
        if (running && !daemon.hello().version.empty()) j.field("version", daemon.hello().version);
        else j.null("version");
        j.field("socket", c.o.socket_path).end_object();
        j.begin_object("database").field("path", d.path).end_object();
        j.begin_object("cycle")
            .field("start", iso_time(q.cycle_start))
            .field("end", iso_time(q.cycle_end))
            .field("renewal_day", q.cycle_day)
            .field("used_bytes", q.used);
        if (q.cap) j.field("cap_bytes", q.cap); else j.null("cap_bytes");
        j.field("projected_bytes", q.projected).end_object();
        if (live) {
            j.begin_object("live")
                .field("ts", iso_time(live->ts))
                .field("down_bytes_per_s", live_down)
                .field("up_bytes_per_s", live_up)
                .end_object();
        } else {
            j.null("live");
        }
        j.end_object();
        out(j.str() + "\n");
        return kExitOk;
    }

    std::string daemon_text;
    switch (state) {
    case DaemonSocket::State::Running:
        daemon_text = daemon.hello().version.empty() ? "running (version not reported)"
                                                     : "running, version " + daemon.hello().version;
        break;
    case DaemonSocket::State::NotRunning: daemon_text = "not running; showing history only"; break;
    case DaemonSocket::State::NoAccess:
        daemon_text = "cannot connect to " + c.o.socket_path + " (" + daemon.error() + ")";
        break;
    case DaemonSocket::State::NoAnswer:
        daemon_text = "not answering at " + c.o.socket_path + " (" + daemon.error() + ")";
        break;
    }
    std::string s;
    const auto line = [&](const std::string& k, const std::string& v) {
        s += c.st.bold(k) + std::string(12 - k.size(), ' ') + v + "\n";
    };
    line("bandsightd", daemon_text);
    line("Database", d.path);
    line("Cycle", local_date(q.cycle_start) + " to " + local_date(q.cycle_end) + " (renews on day " +
                      std::to_string(q.cycle_day) + ")");
    if (q.cap) {
        line("Used", c.volume(q.used) + " of " + c.volume(q.cap) + " cap (" +
                         std::to_string(q.used * 100 / q.cap) + "%)");
        line("Projected", c.volume(q.projected) + " by " + local_date(q.cycle_end) +
                              (q.projected > q.cap ? ", over the cap" : ""));
    } else {
        line("Used", c.volume(q.used) + " (no cap set)");
        line("Projected", c.volume(q.projected) + " by " + local_date(q.cycle_end));
    }
    if (running)
        line("Now", live ? c.st.down("down") + " " + c.rate_of(live_down) + ", " + c.st.up("up") +
                               " " + c.rate_of(live_up)
                         : "no sample yet");
    out(s);
    return kExitOk;
}

// --- live ----------------------------------------------------------------------

std::string clock_time(std::int64_t t) {
    const std::string m = iso_time(t);   // 2026-11-05T12:00:30+00:00
    return m.substr(11, 8);
}

std::string rjust(const std::string& s, std::size_t w) {
    return text_width(s) >= w ? s : std::string(w - text_width(s), ' ') + s;
}

int cmd_live(const Ctx& c) {
    DaemonSocket daemon;
    switch (daemon.connect(c.o.socket_path, 3000)) {
    case DaemonSocket::State::Running: break;
    case DaemonSocket::State::NotRunning:
        err("bandsightd is not running (no daemon at " + c.o.socket_path +
            "). Start it with: sudo systemctl start bandsightd");
        return kExitRuntime;
    case DaemonSocket::State::NoAccess:
        err("Cannot connect to " + c.o.socket_path + ": " + daemon.error() +
            ". Add yourself to the 'bandsight' group (sudo usermod -aG bandsight $USER), then log in again.");
        return kExitRuntime;
    case DaemonSocket::State::NoAnswer:
        err("bandsightd did not answer at " + c.o.socket_path + ": " + daemon.error() + ".");
        return kExitRuntime;
    }
    std::map<int, std::string> names;
    for (const auto& i : daemon.hello().ifaces) names[i.id] = i.name;
    if (!c.o.json)
        err("Live rates from bandsightd" +
            (daemon.hello().version.empty() ? std::string() : " " + daemon.hello().version) +
            ". Ctrl-C to stop.");

    const long per_line = long(c.o.interval);
    long frames = 0, lines = 0;
    std::map<int, std::pair<std::uint64_t, std::uint64_t>> acc;
    std::uint64_t down = 0, up = 0;
    for (;;) {
        if (g_stop) return kExitOk;
        core::Sample s;
        switch (daemon.next_sample(500, s)) {
        case DaemonSocket::Next::Sample: break;
        case DaemonSocket::Next::Timeout: continue;
        case DaemonSocket::Next::Interrupted: continue;   // g_stop is checked above
        case DaemonSocket::Next::Closed:
            err("bandsightd closed the connection.");
            return kExitRuntime;
        case DaemonSocket::Next::Corrupt:
            err("bandsightd sent something that could not be read: " + daemon.error() + ".");
            return kExitRuntime;
        }
        // An interface missing from a frame moved nothing that second.
        for (const auto& i : s.ifaces) {
            acc[i.id].first += i.rx;
            acc[i.id].second += i.tx;
            down += i.rx;
            up += i.tx;
        }
        if (++frames < per_line) continue;

        // One frame per second, so the average rate is the sum over the frames.
        const auto avg = [&](std::uint64_t v) { return v / std::uint64_t(frames); };
        if (c.o.json) {
            Json j;
            j.begin_object()
                .field("ts", iso_time(s.ts))
                .field("interval_s", std::int64_t(frames))
                .field("down_bytes_per_s", avg(down))
                .field("up_bytes_per_s", avg(up));
            j.begin_array("interfaces");
            for (const auto& [id, b] : acc) {
                j.begin_object().field("id", id).field("name", names.count(id) ? names[id] : "")
                    .field("down_bytes_per_s", avg(b.first))
                    .field("up_bytes_per_s", avg(b.second))
                    .end_object();
            }
            j.end_array().end_object();
            out(j.str() + "\n");
        } else {
            out(clock_time(s.ts) + "  " + c.st.down("down") + " " + rjust(c.rate_of(avg(down)), 11) +
                "  " + c.st.up("up") + " " + rjust(c.rate_of(avg(up)), 11) + "\n");
        }
        std::fflush(stdout);
        frames = 0;
        down = up = 0;
        acc.clear();
        if (c.o.count && ++lines >= c.o.count) return kExitOk;
    }
}

}  // namespace

void request_stop() { g_stop = 1; }

int run(const std::vector<std::string>& argv, const Env& env) {
    std::string why;
    const auto o = parse_args(argv, &why);
    if (!o) {
        err(why);
        return kExitUsage;
    }
    if (o->help) {
        out(help_text(o->command));
        return kExitOk;
    }
    if (o->version) {
        out(version_text());
        return kExitOk;
    }
    Ctx c{*o, env, Style{env.tty && !env.no_color && !o->json},
          o->bytes ? core::RateUnit::Bytes : core::RateUnit::Bits,
          o->binary ? core::VolumeUnit::Binary : core::VolumeUnit::Decimal};
    switch (o->command) {
    case Command::Status: return cmd_status(c);
    case Command::Apps: return cmd_apps(c);
    case Command::Hosts: return cmd_hosts(c);
    case Command::Types: return cmd_types(c);
    case Command::Ifaces: return cmd_ifaces(c);
    case Command::Live: return cmd_live(c);
    case Command::None: break;
    }
    return kExitUsage;
}

}  // namespace bandsight::cli
