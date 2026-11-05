// SPDX-License-Identifier: GPL-3.0-or-later
// Strings here are built by appending pieces (cat(), +=), never with
// "literal" + std::string or insert(): GCC 12 reports a false -Wrestrict on
// those, which is fatal under -Werror on Debian 12.
#include "top_view.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <map>

#include "args.h"
#include "core/app_labels.h"
#include "core/service_names.h"
#include "core/unattributed.h"
#include "core/units.h"
#include "output.h"
#include "time_buckets.h"

namespace bandsight::cli {

namespace {

template <class... T>
std::string cat(const T&... parts) {
    std::string s;
    (s += ... += parts);
    return s;
}

// --- history -------------------------------------------------------------------

bool measured_kind(const std::string& kind) { return kind == "physical" || kind == "wifi"; }

int cycle_day(core::HistoryDb& db) {
    const auto v = db.config_value("cycle_start_day");
    const auto d = v ? core::parse_i64(*v) : std::nullopt;
    return d && *d >= 1 && *d <= 31 ? int(*d) : 1;
}

// The bucket span apps and interfaces read (as bandsight-cli apps does).
core::Span bucket_span(const Period& p, std::int64_t now) {
    if (p.kind == Period::Kind::Window) return core::window_span(p.res, p.range_s, now);
    return {bandsight::bucket_start(p.from, p.res), p.to};
}

std::string failed(const char* what, const core::HistoryDb& db) {
    return cat("Cannot read ", what, ": ", db.last_query_error());
}

void sort_rows(std::vector<PanelRow>& rows) {
    std::stable_sort(rows.begin(), rows.end(), [](const PanelRow& a, const PanelRow& b) {
        return a.down + a.up > b.down + b.up;
    });
}

void add_unattributed(Panel& p, const core::Unattributed& u) {
    if (u.shown) p.rows.push_back({"Unattributed", u.down, u.up, true});
}

}  // namespace

void add_sample(LiveView& v, const core::Sample& s) {
    std::uint64_t down = 0, up = 0;
    for (const auto& i : s.ifaces) {
        down += i.rx;
        up += i.tx;
    }
    v.have_rate = true;
    v.down = down;
    v.up = up;
    v.spark.emplace_back(down, up);
    while (v.spark.size() > kSparkSeconds) v.spark.pop_front();
}

HistoryView load_history(core::HistoryDb& db, std::size_t period, std::int64_t now) {
    HistoryView h;
    Options o;
    o.period = top_periods()[period];
    std::string why;
    const auto p = resolve_period(o, now, cycle_day(db), &why);
    if (!p) {
        h.error = why;
        return h;
    }
    h.ok = true;
    h.period_label = p->label;
    h.quota = db.quota(now);

    // The measured interfaces' totals: what a breakdown is short of.
    const core::Span span = bucket_span(*p, now);
    bool headline = false;
    std::uint64_t head_down = 0, head_up = 0;
    if (const auto ifs = db.iface_totals(span.from, span.to, p->res)) {
        headline = true;
        for (const auto& i : *ifs)
            if (measured_kind(i.kind)) {
                head_down += i.rx;
                head_up += i.tx;
            }
    }

    // Applications.
    {
        Panel& panel = h.panels[std::size_t(PanelId::Apps)];
        panel.empty_text = "No application traffic in this period.";
        const auto apps = db.app_totals(span.from, span.to, p->res);
        if (!apps) {
            panel.empty_text = failed("application totals", db);
        } else {
            std::vector<core::LabelledExe> in;
            std::uint64_t down = 0, up = 0;
            for (const auto& a : *apps) {
                in.push_back({core::app_display_name(a.exe, a.name), a.exe});
                down += a.rx;
                up += a.tx;
            }
            const auto labels = core::disambiguated_labels(in);
            for (std::size_t i = 0; i < apps->size(); ++i)
                panel.rows.push_back({labels[i], (*apps)[i].rx, (*apps)[i].tx, false});
            if (headline) add_unattributed(panel, core::unattributed(head_down, head_up, down, up));
        }
    }

    // Hosts: the (application, host) pairs folded into one row per host.
    {
        Panel& panel = h.panels[std::size_t(PanelId::Hosts)];
        panel.empty_text = "No host traffic in this period.";
        const core::HostPairs r = p->kind == Period::Kind::Window
                                      ? db.host_pairs_window(p->res, p->range_s, now)
                                      : db.host_pairs_range(p->from, p->to, p->res, now);
        if (!r.ok) {
            panel.empty_text = failed("host totals", db);
        } else if (!r.collected) {
            panel.empty_text = "This database has no host breakdown. Update bandsightd to collect hosts.";
        } else {
            std::map<std::string, PanelRow> by_ip;
            std::uint64_t down = 0, up = 0;
            for (const auto& pr : r.pairs) {
                down += pr.rx;
                up += pr.tx;
                PanelRow& row = by_ip[pr.ip];
                row.label = !pr.hostname.empty() ? pr.hostname
                            : pr.ip.empty()      ? std::string("No peer address")
                                                 : pr.ip;
                row.down += pr.rx;
                row.up += pr.tx;
            }
            for (auto& [ip, row] : by_ip) panel.rows.push_back(row);
            sort_rows(panel.rows);
            // Not when host recording began inside the period, or there is
            // none: the shortfall is then expected.
            if (headline && r.since <= 0 && !r.pairs.empty())
                add_unattributed(panel, core::unattributed(head_down, head_up, down, up));
            if (r.since > 0) panel.note = cat("recorded since ", local_minutes(r.since));
        }
    }

    // Traffic types, one row per service name (DNS over TCP and UDP is one).
    {
        Panel& panel = h.panels[std::size_t(PanelId::Types)];
        panel.empty_text = "No traffic in this period.";
        const core::TypeTotals r = p->kind == Period::Kind::Window
                                       ? db.type_totals_window(p->res, p->range_s, now)
                                       : db.type_totals_range(p->from, p->to, p->res, now);
        if (!r.ok) {
            panel.empty_text = failed("traffic types", db);
        } else if (!r.collected) {
            panel.empty_text = "This database has no traffic types. Update bandsightd to see them.";
        } else {
            std::map<std::string, PanelRow> by_name;
            for (const auto& t : r.totals) {
                const std::string name = core::service_name(t.proto, t.port);
                PanelRow& row = by_name[name];
                row.label = name;
                row.down += t.rx;
                row.up += t.tx;
            }
            for (auto& [name, row] : by_name) panel.rows.push_back(row);
            sort_rows(panel.rows);
            if (r.since > 0) panel.note = cat("recorded since ", local_minutes(r.since));
        }
    }
    return h;
}

// --- layout --------------------------------------------------------------------

namespace {
constexpr int kHeaderRows = 3;   // daemon, now, cycle
constexpr int kSparkRows = 3;    // title, down, up
constexpr int kPanelChrome = 2;  // title, column header
constexpr int kMinList = 2;      // rows a panel needs to be worth showing
}  // namespace

Layout layout_for(int cols, int rows, std::size_t focus) {
    Layout l;
    l.cols = cols;
    l.rows = rows;
    if (cols < kMinCols || rows < kMinRows) {
        l.too_small = true;
        return l;
    }
    // Header, a blank, [sparkline, a blank,] panels, footer.
    const int with_spark = rows - 1 - (kHeaderRows + 1 + kSparkRows + 1);
    const int without = rows - 1 - (kHeaderRows + 1);
    constexpr int three = int(kPanels) * (kPanelChrome + kMinList);
    int area;
    if (with_spark >= three) {
        l.spark = true;
        area = with_spark;
    } else {
        area = without;
    }
    l.panels_top = rows - 1 - area;
    if (area >= three) {
        const int each = area / int(kPanels);
        for (std::size_t i = 0; i < kPanels; ++i) l.visible[i] = each - kPanelChrome;
        l.visible[focus] += area % int(kPanels);
    } else {
        // Only the focused panel fits.
        l.visible[focus] = area - kPanelChrome;
    }
    return l;
}

void clamp_scroll(TopState& s, const TopData& d, const Layout& l) {
    for (std::size_t i = 0; i < kPanels; ++i) {
        const std::size_t n = d.history.panels[i].rows.size();
        const std::size_t vis = std::size_t(std::max(l.visible[i], 0));
        const std::size_t max = n > vis ? n - vis : 0;
        s.scroll[i] = std::min(s.scroll[i], max);
    }
}

// --- rendering -----------------------------------------------------------------

namespace {

enum class Tone { Plain, Bold, Dim, Down, Up, Warn, Bar };

const char* sgr(Tone t) {
    switch (t) {
    case Tone::Plain: return "";
    case Tone::Bold: return "1";
    case Tone::Dim: return "2";
    case Tone::Down: return "34";   // the GUI's download hue, as near as ANSI gets
    case Tone::Up: return "33";
    case Tone::Warn: return "31";
    case Tone::Bar: return "7";     // reverse: the focused title, the footer
    }
    return "";
}

// The first n code points of s.
std::string prefix(const std::string& s, std::size_t n) {
    std::size_t at = 0, seen = 0;
    while (at < s.size()) {
        if ((static_cast<unsigned char>(s[at]) & 0xC0) != 0x80) {
            if (seen == n) break;
            ++seen;
        }
        ++at;
    }
    return s.substr(0, at);
}

// One screen row: styled pieces, never wider than cols cells.
class Line {
public:
    explicit Line(int cols, bool unicode = true) : cols_(std::size_t(cols)), unicode_(unicode) {}

    Line& add(const std::string& text, Tone tone = Tone::Plain) {
        if (used_ >= cols_ || text.empty()) return *this;
        std::string t = text;
        std::size_t w = text_width(t);
        if (used_ + w > cols_) {
            // Cut short, and marked so: the last cell becomes an ellipsis.
            const std::size_t room = cols_ - used_;
            t = prefix(t, room - 1);
            t += unicode_ ? "…" : "~";
            w = room;
        }
        segs_.push_back({std::move(t), tone});
        used_ += w;
        return *this;
    }
    Line& spaces(std::size_t n, Tone tone = Tone::Plain) { return add(std::string(n, ' '), tone); }
    Line& pad_to(std::size_t col, Tone tone = Tone::Plain) {
        return col > used_ ? spaces(col - used_, tone) : *this;
    }
    // text ending at the last column, if it fits after what is there.
    Line& right(const std::string& text, Tone tone = Tone::Plain, Tone fill = Tone::Plain) {
        const std::size_t w = text_width(text);
        if (used_ + 1 + w > cols_) return *this;
        pad_to(cols_ - w, fill);
        return add(text, tone);
    }
    // Every piece's tone becomes `tone` where it had none.
    Line& fill(Tone tone) {
        fill_ = tone;
        return *this;
    }
    std::size_t used() const { return used_; }

    std::string str(bool colour) const {
        std::string out;
        const auto open = [&](Tone t) {
            if (!colour) return;
            const bool base = fill_ != Tone::Plain;
            if (t == Tone::Plain && !base) return;
            out += "\033[";
            if (base) out += sgr(fill_);
            if (t != Tone::Plain && t != fill_) {
                if (base) out += ';';
                out += sgr(t);
            }
            out += 'm';
        };
        const auto close = [&](Tone t) {
            if (colour && (t != Tone::Plain || fill_ != Tone::Plain)) out += "\033[0m";
        };
        for (const Seg& s : segs_) {
            open(s.tone);
            out += s.text;
            close(s.tone);
        }
        if (used_ < cols_) {
            open(Tone::Plain);
            out += std::string(cols_ - used_, ' ');
            close(Tone::Plain);
        }
        return out;
    }

private:
    struct Seg {
        std::string text;
        Tone tone;
    };
    std::size_t cols_;
    bool unicode_;
    std::size_t used_ = 0;
    std::vector<Seg> segs_;
    Tone fill_ = Tone::Plain;
};

std::string fit(const std::string& s, std::size_t w, bool unicode) {
    if (text_width(s) <= w) return s;
    if (w == 0) return {};
    return cat(prefix(s, w - 1), unicode ? "…" : "~");
}

std::string rjust(const std::string& s, std::size_t w) {
    const std::size_t n = text_width(s);
    std::string out;
    if (n < w) out.assign(w - n, ' ');
    out += s;
    return out;
}

std::string clock_text(std::int64_t t) {
    const std::time_t tt = std::time_t(t);
    std::tm tm{};
    localtime_r(&tt, &tm);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

struct Painter {
    const TopState& s;
    const TopData& d;
    const Layout& l;
    const Look& look;

    Line line() const { return Line(l.cols, look.unicode); }
    std::string rate(std::uint64_t b) const { return core::format_rate(b, s.rate); }
    std::string volume(std::uint64_t b) const { return core::format_volume(b, s.volume); }

    // --- header ---

    Line daemon_line() const {
        Line ln = line();
        ln.add("Bandsight", Tone::Bold).spaces(2);
        const LiveView& v = d.live;
        switch (v.state) {
        case DaemonSocket::State::Running:
            ln.add(v.version.empty() ? std::string("bandsightd running")
                                     : cat("bandsightd ", v.version, " running"));
            break;
        case DaemonSocket::State::NotRunning:
            ln.add("bandsightd is not running - history only", Tone::Warn);
            break;
        case DaemonSocket::State::NoAccess:
            ln.add(cat("cannot connect to bandsightd (", v.error, ") - history only"), Tone::Warn);
            break;
        case DaemonSocket::State::NoAnswer:
            ln.add(cat("bandsightd is not answering (", v.error, ") - history only"), Tone::Warn);
            break;
        }
        ln.right(clock_text(d.now), Tone::Dim);
        return ln;
    }

    Line now_line() const {
        Line ln = line();
        ln.add("Now", Tone::Bold).spaces(5);
        const LiveView& v = d.live;
        if (v.state != DaemonSocket::State::Running) {
            ln.add("no live rates", Tone::Dim);
        } else if (!v.have_rate) {
            ln.add("waiting for the first sample", Tone::Dim);
        } else {
            ln.add("down", Tone::Down).add(" ").add(rjust(rate(v.down), 11));
            ln.spaces(3).add("up", Tone::Up).add(" ").add(rjust(rate(v.up), 11));
        }
        return ln;
    }

    Line cycle_line() const {
        Line ln = line();
        ln.add("Cycle", Tone::Bold).spaces(3);
        const HistoryView& h = d.history;
        if (!h.ok || !h.quota) {
            ln.add(h.error.empty() ? std::string("no history") : h.error, Tone::Warn);
            return ln;
        }
        const core::Quota& q = *h.quota;
        if (q.cap) {
            ln.add(cat(volume(q.used), " of ", volume(q.cap), " (",
                       std::to_string(q.used * 100 / q.cap), "%)"));
        } else {
            ln.add(cat(volume(q.used), " used, no cap set"));
        }
        ln.add(cat(", projected ", volume(q.projected), " by ", local_date(q.cycle_end)));
        if (q.cap && q.projected > q.cap) ln.add(", over the cap", Tone::Warn);
        return ln;
    }

    // --- sparkline ---

    std::string spark(bool down, std::size_t width, std::uint64_t& peak) const {
        static const char* const blocks[] = {" ", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
        static const char ascii[] = " .:-=+*#@";
        const auto& sp = d.live.spark;
        const std::size_t n = std::min(sp.size(), width);
        peak = 0;
        for (std::size_t i = sp.size() - n; i < sp.size(); ++i)
            peak = std::max(peak, down ? sp[i].first : sp[i].second);
        std::string out(width - n, ' ');
        for (std::size_t i = sp.size() - n; i < sp.size(); ++i) {
            const std::uint64_t v = down ? sp[i].first : sp[i].second;
            int level = 0;
            if (v > 0 && peak > 0)
                level = std::clamp(int(std::ceil(double(v) * 8.0 / double(peak))), 1, 8);
            if (look.unicode) out += blocks[level];
            else out += ascii[level];
        }
        return out;
    }

    void spark_lines(std::vector<Line>& out) const {
        Line title = line();
        title.add(cat("Last ", std::to_string(kSparkSeconds), " s"), Tone::Bold);
        out.push_back(title);
        const bool live = d.live.state == DaemonSocket::State::Running;
        // "down " + the graph + "  peak 999.9 Mb/s"
        const std::size_t room = std::size_t(l.cols) > 5 + 2 + 16 ? std::size_t(l.cols) - 5 - 2 - 16 : 0;
        const std::size_t width = std::min(kSparkSeconds, room);
        for (const bool down : {true, false}) {
            Line ln = line();
            ln.add(down ? "down" : "up", down ? Tone::Down : Tone::Up).pad_to(5);
            if (!live) {
                if (down) ln.add("no live data while bandsightd is not running", Tone::Dim);
            } else if (d.live.spark.empty()) {
                if (down) ln.add("waiting for samples", Tone::Dim);
            } else {
                std::uint64_t peak = 0;
                ln.add(spark(down, width, peak), down ? Tone::Down : Tone::Up);
                ln.spaces(2).add(cat("peak ", rate(peak)), Tone::Dim);
            }
            out.push_back(ln);
        }
    }

    // --- panels ---

    // The name column fits the longest label in any of the lists, so the
    // columns line up from panel to panel and do not jump while scrolling.
    std::size_t name_width() const {
        std::size_t w = 0;
        for (const Panel& p : d.history.panels)
            for (const PanelRow& r : p.rows) w = std::max(w, text_width(r.label));
        return std::clamp<std::size_t>(w, 14, 32);
    }

    void panel_lines(std::size_t id, std::vector<Line>& out) const {
        static const char* const titles[] = {"Applications", "Hosts", "Traffic types"};
        static const char* const heads[] = {"APPLICATION", "HOST", "SERVICE"};
        const Panel& p = d.history.panels[id];
        const bool focused = s.focus == id;
        const std::size_t vis = std::size_t(l.visible[id]);
        const std::size_t first = s.scroll[id];

        Line title = line();
        if (focused) title.fill(Tone::Bar);
        title.add(focused ? (look.unicode ? "▶ " : "> ") : "  ")
            .add(cat(std::to_string(id + 1), " ", titles[id]), Tone::Bold);
        if (!d.history.period_label.empty()) title.add(cat("  ", d.history.period_label));
        if (!p.note.empty()) title.add(cat(", ", p.note), Tone::Dim);
        if (p.rows.size() > vis) {
            const std::size_t last = std::min(first + vis, p.rows.size());
            title.right(cat(std::to_string(first + 1), "-", std::to_string(last), " of ",
                            std::to_string(p.rows.size()), " "),
                        Tone::Plain, Tone::Plain);
        }
        out.push_back(title);

        // Columns: name, down, up, bar.
        constexpr std::size_t kNum = 9, kGap = 2;
        const std::size_t cols = std::size_t(l.cols);
        std::size_t name_w = name_width();
        const std::size_t fixed = 1 + kGap + kNum + kGap + kNum;
        name_w = std::min(name_w, cols > fixed + 1 ? cols - fixed - 1 : 1);
        std::size_t bar_w = cols > 1 + name_w + fixed + kGap + 1 ? cols - (1 + name_w + fixed + kGap + 1) : 0;
        if (bar_w < 4) bar_w = 0;
        bar_w = std::min<std::size_t>(bar_w, 40);

        Line head = line();
        head.add(" ").add(heads[id], Tone::Dim).pad_to(1 + name_w + kGap)
            .add(rjust("DOWN", kNum), Tone::Dim).spaces(kGap).add(rjust("UP", kNum), Tone::Dim);
        out.push_back(head);

        std::uint64_t biggest = 0;
        for (const PanelRow& r : p.rows) biggest = std::max(biggest, r.down + r.up);

        for (std::size_t i = 0; i < vis; ++i) {
            Line ln = line();
            const std::size_t at = first + i;
            if (p.rows.empty() && i == 0) {
                ln.add(" ").add(p.empty_text, Tone::Dim);
            } else if (at < p.rows.size()) {
                const PanelRow& r = p.rows[at];
                const Tone t = r.muted ? Tone::Dim : Tone::Plain;
                ln.add(" ").add(fit(r.label, name_w, look.unicode), t).pad_to(1 + name_w + kGap);
                ln.add(rjust(volume(r.down), kNum), t).spaces(kGap).add(rjust(volume(r.up), kNum), t);
                if (bar_w && biggest) bar(ln.spaces(kGap), r, biggest, bar_w);
            }
            out.push_back(ln);
        }
    }

    // Down then up, each rounded on its own, at least one cell for a direction
    // that moved anything (as bandsight-cli's tables draw them).
    void bar(Line& ln, const PanelRow& r, std::uint64_t biggest, std::size_t width) const {
        const auto cells = [&](std::uint64_t v) -> std::size_t {
            if (v == 0) return 0;
            return std::max<std::size_t>(1, std::size_t(double(v) / double(biggest) * double(width) + 0.5));
        };
        std::size_t dn = cells(r.down), up = cells(r.up);
        // Rounding both up can overshoot by a cell: take it from the larger.
        if (dn + up > width) {
            if (dn >= up) dn = width - up;
            else up = width - dn;
        }
        // Without colour the two directions need different glyphs.
        const char* dch = look.unicode ? "█" : "#";
        const char* uch = look.colour ? dch : look.unicode ? "░" : "=";
        std::string a, b;
        for (std::size_t i = 0; i < dn; ++i) a += dch;
        for (std::size_t i = 0; i < up; ++i) b += uch;
        ln.add(a, Tone::Down).add(b, Tone::Up);
    }

    Line footer() const {
        Line ln = line();
        ln.fill(Tone::Bar);
        ln.add(" 1/2/3 Tab", Tone::Bold).add(" panel  ");
        ln.add("p", Tone::Bold).add(cat(" period (", top_periods()[s.period], ")  "));
        ln.add("b", Tone::Bold).add(s.rate == core::RateUnit::Bits ? " units (Mb/s)  " : " units (MB/s)  ");
        ln.add(look.unicode ? "↑↓ j/k" : "Up/Dn j/k", Tone::Bold).add(" scroll  ");
        ln.add("q", Tone::Bold).add(" quit");
        ln.pad_to(std::size_t(l.cols));
        return ln;
    }
};

}  // namespace

std::vector<std::string> render_top(const TopState& s, const TopData& d, const Layout& l,
                                    const Look& look) {
    std::vector<Line> lines;
    if (l.too_small) {
        for (int r = 0; r < l.rows; ++r) lines.emplace_back(l.cols, look.unicode);
        if (l.rows > 0) {
            const std::string msg = cat("Terminal too small: needs ", std::to_string(kMinCols), "x",
                                        std::to_string(kMinRows), ", q quits");
            Line& ln = lines[std::size_t(l.rows / 2)];
            const std::size_t w = text_width(msg);
            if (w < std::size_t(l.cols)) ln.spaces((std::size_t(l.cols) - w) / 2);
            ln.add(msg, Tone::Warn);
        }
    } else {
        const Painter p{s, d, l, look};
        lines.push_back(p.daemon_line());
        lines.push_back(p.now_line());
        lines.push_back(p.cycle_line());
        lines.emplace_back(l.cols);
        if (l.spark) {
            p.spark_lines(lines);
            lines.emplace_back(l.cols);
        }
        for (std::size_t i = 0; i < kPanels; ++i)
            if (l.visible[i] > 0) p.panel_lines(i, lines);
        while (int(lines.size()) < l.rows - 1) lines.emplace_back(l.cols);
        lines.resize(std::size_t(l.rows - 1), Line(l.cols));
        lines.push_back(p.footer());
    }
    std::vector<std::string> out;
    out.reserve(lines.size());
    for (const Line& ln : lines) out.push_back(ln.str(look.colour));
    return out;
}

}  // namespace bandsight::cli
