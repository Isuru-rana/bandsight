// SPDX-License-Identifier: GPL-3.0-or-later
#include "top_input.h"

#include <cstdlib>

namespace bandsight::cli {

namespace {

// The key a complete CSI or SS3 sequence stands for. `params` is what came
// between the introducer and the final byte.
Key csi_key(char final, const std::string& params) {
    switch (final) {
    case 'A': return {KeyCode::Up};
    case 'B': return {KeyCode::Down};
    case 'H': return {KeyCode::Home};
    case 'F': return {KeyCode::End};
    case 'Z': return {KeyCode::BackTab};
    case '~':
        // "5~" and also "5;2~" (with modifiers): the number before any ';'.
        switch (std::atoi(params.c_str())) {
        case 1: case 7: return {KeyCode::Home};
        case 4: case 8: return {KeyCode::End};
        case 5: return {KeyCode::PageUp};
        case 6: return {KeyCode::PageDown};
        default: break;
        }
        break;
    default: break;
    }
    return {KeyCode::Unknown};
}

}  // namespace

std::vector<Key> KeyDecoder::feed(std::string_view bytes) {
    pending_.append(bytes.data(), bytes.size());
    std::vector<Key> out;
    std::size_t i = 0;
    while (i < pending_.size()) {
        const unsigned char c = static_cast<unsigned char>(pending_[i]);
        if (c != 0x1b) {
            if (c == 0x03) out.push_back({KeyCode::CtrlC});
            else if (c == '\t') out.push_back({KeyCode::Tab});
            else if (c == '\r' || c == '\n') out.push_back({KeyCode::Enter});
            else out.push_back({KeyCode::Char, char(c)});
            ++i;
            continue;
        }
        // ESC: a sequence, or the key itself.
        if (i + 1 >= pending_.size()) break;   // wait for more, or flush()
        const char intro = pending_[i + 1];
        if (intro != '[' && intro != 'O') {
            // ESC then an ordinary key (Alt+key): the escape, then the key.
            out.push_back({KeyCode::Escape});
            ++i;
            continue;
        }
        // Parameter and intermediate bytes, then a final byte in 0x40..0x7e.
        std::size_t j = i + 2;
        while (j < pending_.size() &&
               !(static_cast<unsigned char>(pending_[j]) >= 0x40 &&
                 static_cast<unsigned char>(pending_[j]) <= 0x7e))
            ++j;
        if (j >= pending_.size()) {
            // Incomplete. A sequence this long is not one: drop it.
            if (j - i > 16) {
                out.push_back({KeyCode::Unknown});
                i = j;
                continue;
            }
            break;
        }
        out.push_back(csi_key(pending_[j], pending_.substr(i + 2, j - i - 2)));
        i = j + 1;
    }
    pending_.erase(0, i);
    return out;
}

std::vector<Key> KeyDecoder::flush() {
    std::vector<Key> out;
    if (pending_.empty()) return out;
    // A lone ESC is the Escape key; a cut-off sequence is nothing anyone typed.
    out.push_back(pending_ == "\033" ? Key{KeyCode::Escape} : Key{KeyCode::Unknown});
    pending_.clear();
    return out;
}

const std::vector<const char*>& top_periods() {
    static const std::vector<const char*> p{"5m", "1h", "24h", "7d", "30d", "cycle"};
    return p;
}

std::size_t top_period_index(const std::string& name) {
    const auto& p = top_periods();
    for (std::size_t i = 0; i < p.size(); ++i)
        if (name == p[i]) return i;
    return std::string::npos;
}

std::size_t next_period(std::size_t i, int step) {
    const long n = long(top_periods().size());
    return std::size_t(((long(i) + step) % n + n) % n);
}

Action apply_key(TopState& s, const Key& k, std::size_t page) {
    std::size_t& top = s.scroll[s.focus];
    const std::size_t step = page > 1 ? page - 1 : 1;
    switch (k.code) {
    case KeyCode::CtrlC: return Action::Quit;
    case KeyCode::Tab: s.focus = (s.focus + 1) % kPanels; return Action::Redraw;
    case KeyCode::BackTab: s.focus = (s.focus + kPanels - 1) % kPanels; return Action::Redraw;
    case KeyCode::Up: if (top == 0) return Action::None; --top; return Action::Redraw;
    case KeyCode::Down: ++top; return Action::Redraw;
    case KeyCode::PageUp: top = top > step ? top - step : 0; return Action::Redraw;
    case KeyCode::PageDown: top += step; return Action::Redraw;
    case KeyCode::Home: top = 0; return Action::Redraw;
    case KeyCode::End: top = kScrollEnd; return Action::Redraw;
    case KeyCode::Char: break;
    case KeyCode::Enter:
    case KeyCode::Escape:
    case KeyCode::Unknown: return Action::None;
    }
    switch (k.ch) {
    case 'q': case 'Q': return Action::Quit;
    case '1': case '2': case '3': {
        const std::size_t want = std::size_t(k.ch - '1');
        if (want == s.focus) return Action::None;
        s.focus = want;
        return Action::Redraw;
    }
    case 'p':
    case 'P':
        s.period = next_period(s.period, k.ch == 'p' ? 1 : -1);
        // A new period is new rows: start each list at its top.
        s.scroll = {};
        return Action::Reload;
    case 'b':
    case 'B':
        s.rate = s.rate == core::RateUnit::Bits ? core::RateUnit::Bytes : core::RateUnit::Bits;
        return Action::Redraw;
    case 'k': return apply_key(s, {KeyCode::Up}, page);
    case 'j': return apply_key(s, {KeyCode::Down}, page);
    case 'g': return apply_key(s, {KeyCode::Home}, page);
    case 'G': return apply_key(s, {KeyCode::End}, page);
    default: break;
    }
    return Action::None;
}

}  // namespace bandsight::cli
