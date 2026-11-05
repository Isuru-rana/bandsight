// SPDX-License-Identifier: GPL-3.0-or-later
// `bandsight-cli top`, without a terminal:
//   - the key decoder: plain keys, escape sequences whole and split across
//     reads, a lone ESC, Ctrl-C;
//   - the key state machine: focus, period cycling, units, scrolling, quit.
#include <string>
#include <vector>

#include "check.h"
#include "top_input.h"

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

}  // namespace

int main() {
    decoder();
    periods();
    state_machine();
    if (g_failures) std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    return g_failures ? 1 : 0;
}
