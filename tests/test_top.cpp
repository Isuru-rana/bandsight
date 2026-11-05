// SPDX-License-Identifier: GPL-3.0-or-later
// `bandsight-cli top`, without a terminal:
//   - the key decoder: plain keys, escape sequences whole and split across
//     reads, a lone ESC, Ctrl-C;
//   - the key state machine: focus, period cycling, units, scrolling, quit;
//   - the screen laid out into fixed-size buffers from the fixture database
//     (tests/fixture_db.h) with NO_COLOR, against tests/top_golden.txt (set
//     BANDSIGHT_UPDATE_GOLDEN=1 to rewrite it, then review the diff);
//   - the layout at every size, scroll clamping, and the colour output.
//
// argv: <golden file> <scratch directory>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "check.h"
#include "fixture_db.h"
#include "output.h"
#include "top_input.h"
#include "top_view.h"

using namespace bandsight;
using namespace bandsight::cli;

namespace {

std::vector<Key> keys(const std::string& bytes) {
    KeyDecoder d;
    auto k = d.feed(bytes);
    for (const Key& f : d.flush()) k.push_back(f);
    return k;
}

Key ch(char c) { return {KeyCode::Char, c}; }

void decoder() {
    CHECK(keys("q") == std::vector<Key>{ch('q')});
    CHECK(keys("\x03") == std::vector<Key>{{KeyCode::CtrlC}});
    CHECK(keys("\t") == std::vector<Key>{{KeyCode::Tab}});
    CHECK(keys("\r") == std::vector<Key>{{KeyCode::Enter}});
    CHECK(keys("\033[A\033[B") == (std::vector<Key>{{KeyCode::Up}, {KeyCode::Down}}));
    // Application cursor mode sends SS3 instead of CSI.
    CHECK(keys("\033OA\033OB") == (std::vector<Key>{{KeyCode::Up}, {KeyCode::Down}}));
    CHECK(keys("\033[5~\033[6~") == (std::vector<Key>{{KeyCode::PageUp}, {KeyCode::PageDown}}));
    CHECK(keys("\033[H\033[F\033[1~\033[4~") ==
          (std::vector<Key>{{KeyCode::Home}, {KeyCode::End}, {KeyCode::Home}, {KeyCode::End}}));
    CHECK(keys("\033[Z") == std::vector<Key>{{KeyCode::BackTab}});
    // With a modifier, and unknown ones.
    CHECK(keys("\033[5;2~") == std::vector<Key>{{KeyCode::PageUp}});
    CHECK(keys("\033[1;5C") == std::vector<Key>{{KeyCode::Unknown}});
    // Keys around a sequence.
    CHECK(keys("j\033[Ak") == (std::vector<Key>{ch('j'), {KeyCode::Up}, ch('k')}));
    // Alt+key: the escape, then the key.
    CHECK(keys("\033q") == (std::vector<Key>{{KeyCode::Escape}, ch('q')}));

    // Split across reads: held, then completed.
    {
        KeyDecoder d;
        CHECK(d.feed("\033").empty() && d.pending());
        CHECK(d.feed("[").empty() && d.pending());
        CHECK(d.feed("6").empty());
        CHECK(d.feed("~p") == (std::vector<Key>{{KeyCode::PageDown}, ch('p')}));
        CHECK(!d.pending());
    }
    // A lone ESC is the Escape key once input goes quiet.
    {
        KeyDecoder d;
        CHECK(d.feed("\033").empty());
        CHECK(d.flush() == std::vector<Key>{{KeyCode::Escape}});
        CHECK(!d.pending() && d.flush().empty());
    }
    // Garbage that never ends is dropped, not held forever.
    {
        KeyDecoder d;
        const auto k = d.feed("\033[" + std::string(40, '1'));
        CHECK(!k.empty() && k[0].code == KeyCode::Unknown);
        CHECK(d.feed("q") == std::vector<Key>{ch('q')});
    }
}

void periods() {
    const auto& p = top_periods();
    CHECK(p.size() == 6);
    const std::vector<std::string> want{"5m", "1h", "24h", "7d", "30d", "cycle"};
    for (std::size_t i = 0; i < want.size(); ++i) CHECK(want[i] == p[i]);
    CHECK(top_period_index("24h") == 2 && top_period_index("cycle") == 5);
    CHECK(top_period_index("30m") == std::string::npos);
    CHECK(next_period(0, 1) == 1 && next_period(5, 1) == 0);   // wraps
    CHECK(next_period(0, -1) == 5 && next_period(3, -1) == 2);

    // `p` walks the whole cycle and comes back, reloading each time.
    TopState s;
    std::vector<std::string> seen;
    for (int i = 0; i < 6; ++i) {
        CHECK(apply_key(s, ch('p'), 10) == Action::Reload);
        seen.push_back(p[s.period]);
    }
    CHECK(seen == (std::vector<std::string>{"7d", "30d", "cycle", "5m", "1h", "24h"}));
    CHECK(apply_key(s, ch('P'), 10) == Action::Reload && std::string(p[s.period]) == "1h");
    // A new period starts every list at its top.
    s.scroll = {4, 5, 6};
    apply_key(s, ch('p'), 10);
    CHECK(s.scroll[0] == 0 && s.scroll[1] == 0 && s.scroll[2] == 0);
}

void state_machine() {
    TopState s;
    CHECK(s.focus == 0 && std::string(top_periods()[s.period]) == "24h");

    // Focus: 1/2/3 directly, Tab and Shift-Tab around.
    CHECK(apply_key(s, ch('2'), 10) == Action::Redraw && s.focus == 1);
    CHECK(apply_key(s, ch('2'), 10) == Action::None && s.focus == 1);
    CHECK(apply_key(s, ch('3'), 10) == Action::Redraw && s.focus == 2);
    CHECK(apply_key(s, {KeyCode::Tab}, 10) == Action::Redraw && s.focus == 0);
    CHECK(apply_key(s, {KeyCode::BackTab}, 10) == Action::Redraw && s.focus == 2);
    CHECK(apply_key(s, ch('1'), 10) == Action::Redraw && s.focus == 0);
    CHECK(apply_key(s, ch('4'), 10) == Action::None && s.focus == 0);

    // Units.
    CHECK(s.rate == core::RateUnit::Bits);
    CHECK(apply_key(s, ch('b'), 10) == Action::Redraw && s.rate == core::RateUnit::Bytes);
    CHECK(apply_key(s, ch('b'), 10) == Action::Redraw && s.rate == core::RateUnit::Bits);

    // Scrolling moves the focused panel only.
    CHECK(apply_key(s, ch('j'), 10) == Action::Redraw && s.scroll[0] == 1);
    CHECK(apply_key(s, {KeyCode::Down}, 10) == Action::Redraw && s.scroll[0] == 2);
    CHECK(apply_key(s, ch('k'), 10) == Action::Redraw && s.scroll[0] == 1);
    CHECK(apply_key(s, {KeyCode::Up}, 10) == Action::Redraw && s.scroll[0] == 0);
    CHECK(apply_key(s, {KeyCode::Up}, 10) == Action::None && s.scroll[0] == 0);   // at the top
    CHECK(apply_key(s, {KeyCode::PageDown}, 10) == Action::Redraw && s.scroll[0] == 9);
    CHECK(apply_key(s, {KeyCode::PageUp}, 4) == Action::Redraw && s.scroll[0] == 6);
    CHECK(apply_key(s, {KeyCode::PageUp}, 40) == Action::Redraw && s.scroll[0] == 0);
    CHECK(apply_key(s, ch('G'), 10) == Action::Redraw && s.scroll[0] == kScrollEnd);
    CHECK(apply_key(s, ch('g'), 10) == Action::Redraw && s.scroll[0] == 0);
    apply_key(s, ch('2'), 10);
    apply_key(s, ch('j'), 10);
    CHECK(s.scroll[0] == 0 && s.scroll[1] == 1);

    // Leaving.
    CHECK(apply_key(s, ch('q'), 10) == Action::Quit);
    CHECK(apply_key(s, ch('Q'), 10) == Action::Quit);
    CHECK(apply_key(s, {KeyCode::CtrlC}, 10) == Action::Quit);

    // Everything else is ignored.
    for (const Key& k : {ch('x'), ch(' '), Key{KeyCode::Enter}, Key{KeyCode::Escape},
                         Key{KeyCode::Unknown}})
        CHECK(apply_key(s, k, 10) == Action::None);
}

// --- rendering -----------------------------------------------------------------

std::string g_golden;

// A minute of a download ramping up and easing off, as one frame a second.
LiveView live_running() {
    LiveView v;
    v.state = DaemonSocket::State::Running;
    v.version = "0.5.0";
    for (int i = 0; i < 75; ++i) {
        core::Sample s;
        s.ts = fixture::kFixtureNow - 75 + i + 1;
        const int x = i % 30 < 15 ? i % 30 : 30 - i % 30;
        s.ifaces.push_back({2, std::uint64_t(100'000 * x * x), std::uint64_t(4'000 * (i % 7))});
        if (i % 3 == 0) s.ifaces.push_back({3, 2'000, 500});
        add_sample(v, s);
    }
    return v;
}

TopData data_for(core::HistoryDb& db, std::size_t period, const LiveView& live) {
    TopData d;
    d.now = fixture::kFixtureNow;
    d.live = live;
    d.history = load_history(db, period, d.now);
    return d;
}

std::vector<std::string> screen(TopState& s, const TopData& d, int cols, int rows,
                                const Look& look = {}) {
    const Layout l = layout_for(cols, rows, s.focus);
    clamp_scroll(s, d, l);
    return render_top(s, d, l, look);
}

void golden(const std::string& title, TopState s, const TopData& d, int cols, int rows,
            const Look& look = {}) {
    const auto lines = screen(s, d, cols, rows, look);
    // Exactly the size asked, every line exactly as wide.
    CHECK(int(lines.size()) == rows);
    for (const auto& ln : lines) CHECK(int(text_width(ln)) == cols);
    g_golden += "### ";
    g_golden += title;
    g_golden += " (" + std::to_string(cols) + "x" + std::to_string(rows) + ")\n";
    for (const auto& ln : lines) {
        g_golden += '|';
        g_golden += ln;
        g_golden += "|\n";
    }
    g_golden += '\n';
}

void rendering(const std::string& dir) {
    const std::string path = dir + "/top.db";
    std::filesystem::remove(path);
    fixture::write(path);
    core::HistoryDb db;
    CHECK(db.open(path));

    const LiveView live = live_running();
    CHECK(live.spark.size() == kSparkSeconds);   // the oldest fell off
    CHECK(live.have_rate && live.down == 100'000 * 14 * 14);

    TopState s;
    const TopData day = data_for(db, s.period, live);
    CHECK(day.history.ok && day.history.period_label == "last 24 hours");
    // Apps and hosts end in Unattributed, as in the GUI; types never do.
    const auto& apps = day.history.panels[0].rows;
    CHECK(!apps.empty() && apps.back().label == "Unattributed" && apps.back().muted);
    CHECK(apps.front().label == "Firefox");
    const auto& hosts = day.history.panels[1].rows;
    CHECK(!hosts.empty() && hosts.back().label == "Unattributed");
    for (const auto& r : day.history.panels[2].rows) CHECK(r.label != "Unattributed");

    golden("24 hours, apps focused", s, day, 80, 24);
    golden("a wide, tall terminal", s, day, 120, 40);

    TopState hosts_focus = s;
    hosts_focus.focus = 1;
    golden("hosts focused", hosts_focus, day, 80, 24);

    // Scrolled to the end: clamped so the last row is at the bottom.
    TopState end = s;
    end.scroll[0] = kScrollEnd;
    golden("apps scrolled to the end", end, day, 80, 24);

    TopState bytes = s;
    bytes.rate = core::RateUnit::Bytes;
    golden("MB/s", bytes, day, 80, 24);

    // Each period p steps to.
    for (const char* name : {"5m", "30d", "cycle"}) {
        TopState p = s;
        p.period = top_period_index(name);
        golden(std::string("period ") + name, p, data_for(db, p.period, live), 80, 24);
    }

    // The daemon down: the header says so, the history is still there.
    LiveView down;
    down.state = DaemonSocket::State::NotRunning;
    const TopData offline = data_for(db, s.period, down);
    golden("daemon not running", s, offline, 80, 24);
    // Running, but nothing heard yet.
    LiveView waiting;
    waiting.state = DaemonSocket::State::Running;
    golden("waiting for the first sample", s, data_for(db, s.period, waiting), 80, 24);

    // Small: no sparkline, then only the focused panel, then too small.
    golden("short terminal", s, day, 80, 18);
    TopState types_focus = s;
    types_focus.focus = 2;
    golden("one panel fits", types_focus, day, 60, 13);
    golden("too small", s, day, 40, 12);

    // No unicode: ASCII blocks, bars and markers.
    golden("ASCII", s, day, 80, 24, Look{false, false});

    // A database that cannot be read.
    TopData broken = day;
    broken.history = HistoryView{};
    broken.history.error = "Cannot read /nowhere.db: the file does not exist.";
    for (auto& p : broken.history.panels) p.empty_text = "No history.";
    golden("no database", s, broken, 80, 24);
}

void layout() {
    // Every size from tiny to large: the lines are always exactly the screen,
    // and the panels never run into the footer.
    for (int cols : {10, 49, 50, 80, 200})
        for (int rows : {1, 11, 12, 13, 16, 20, 21, 24, 60}) {
            for (std::size_t focus = 0; focus < kPanels; ++focus) {
                const Layout l = layout_for(cols, rows, focus);
                CHECK(l.too_small == (cols < kMinCols || rows < kMinRows));
                if (l.too_small) continue;
                int used = l.panels_top;
                for (std::size_t i = 0; i < kPanels; ++i) {
                    CHECK(l.visible[i] >= 0);
                    if (l.visible[i] > 0) used += 2 + l.visible[i];
                }
                CHECK(used == rows - 1);
                CHECK(l.visible[focus] > 0);   // the focused panel is always shown
            }
        }
    // 24 rows: all three panels and the sparkline; the focused one gets the rest.
    const Layout l = layout_for(80, 24, 1);
    CHECK(l.spark && l.visible[0] == 3 && l.visible[1] == 3 && l.visible[2] == 3);
    const Layout l2 = layout_for(80, 25, 1);
    CHECK(l2.visible[0] == 3 && l2.visible[1] == 4 && l2.visible[2] == 3);
    const Layout l3 = layout_for(80, 18, 0);
    CHECK(!l3.spark && l3.visible[0] > 0 && l3.visible[1] > 0 && l3.visible[2] > 0);
    const Layout l4 = layout_for(80, 12, 2);
    CHECK(!l4.spark && l4.visible[0] == 0 && l4.visible[1] == 0 && l4.visible[2] == 5);

    // Clamping.
    TopData d;
    d.history.panels[0].rows.resize(10);
    d.history.panels[1].rows.resize(2);
    TopState s;
    s.scroll = {kScrollEnd, 5, 3};
    clamp_scroll(s, d, l);   // 3 rows each
    CHECK(s.scroll[0] == 7 && s.scroll[1] == 0 && s.scroll[2] == 0);
}

void colour() {
    TopData d;
    d.now = fixture::kFixtureNow;
    d.live = live_running();
    d.history.ok = true;
    d.history.panels[0].rows = {{"firefox", 900, 100, false}, {"Unattributed", 50, 0, true}};
    TopState s;
    const Layout l = layout_for(80, 24, 0);
    const auto plain = render_top(s, d, l, Look{false, true});
    const auto fancy = render_top(s, d, l, Look{true, true});
    std::string all_plain, all_fancy;
    for (const auto& x : plain) all_plain += x;
    for (const auto& x : fancy) all_fancy += x;
    // NO_COLOR: not one escape.
    CHECK(all_plain.find('\033') == std::string::npos);
    CHECK(all_fancy.find("\033[34m") != std::string::npos);   // down
    CHECK(all_fancy.find("\033[33m") != std::string::npos);   // up
    CHECK(all_fancy.find("\033[2m") != std::string::npos);    // Unattributed, dimmed
    CHECK(all_fancy.find("\033[7m") != std::string::npos);    // focused title, footer
    // The escapes take no cells: the same text with them removed.
    for (std::size_t i = 0; i < fancy.size(); ++i) {
        std::string stripped;
        for (std::size_t at = 0; at < fancy[i].size(); ++at) {
            if (fancy[i][at] == '\033') {
                at = fancy[i].find('m', at);
                continue;
            }
            stripped += fancy[i][at];
        }
        // Colour draws both bar directions in one glyph; plain needs two.
        std::string p = plain[i];
        for (std::size_t at; (at = p.find("░")) != std::string::npos;) p.replace(at, std::string("░").size(), "█");
        CHECK(stripped == p);
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: test_top <golden file> <scratch directory>\n");
        return 2;
    }
    ::setenv("TZ", "UTC", 1);
    ::tzset();
    const std::string golden_path = argv[1];
    const std::string dir = argv[2];
    std::filesystem::create_directories(dir);

    decoder();
    periods();
    state_machine();
    layout();
    colour();
    rendering(dir);

    if (std::getenv("BANDSIGHT_UPDATE_GOLDEN")) {
        std::ofstream(golden_path, std::ios::binary) << g_golden;
        std::fprintf(stderr, "wrote %s\n", golden_path.c_str());
    } else {
        std::ifstream in(golden_path, std::ios::binary);
        std::stringstream want;
        want << in.rdbuf();
        CHECK(want.str() == g_golden);
        if (want.str() != g_golden) {
            const std::string got = dir + "/top_golden.actual.txt";
            std::ofstream(got, std::ios::binary) << g_golden;
            std::fprintf(stderr, "screens differ from %s; this run's are in %s\n",
                         golden_path.c_str(), got.c_str());
        }
    }
    if (g_failures) std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    return g_failures ? 1 : 0;
}
