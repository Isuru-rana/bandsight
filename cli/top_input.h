// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "core/units.h"

namespace bandsight::cli {

// --- keys ----------------------------------------------------------------------

enum class KeyCode { Char, Up, Down, PageUp, PageDown, Home, End, Tab, BackTab, Enter, Escape,
                     CtrlC, Unknown };

struct Key {
    KeyCode code = KeyCode::Unknown;
    char ch = 0;   // KeyCode::Char only
    bool operator==(const Key&) const = default;
};

// Bytes from a raw-mode terminal to keys. An escape sequence split across two
// reads is held until the rest arrives; a lone ESC stays pending until flush(),
// which the caller makes once input has been quiet for a moment.
class KeyDecoder {
public:
    std::vector<Key> feed(std::string_view bytes);
    std::vector<Key> flush();
    bool pending() const { return !pending_.empty(); }

private:
    std::string pending_;
};

// --- periods -------------------------------------------------------------------

// What `p` steps through, in order, wrapping: the names bandsight-cli's
// --period takes.
const std::vector<const char*>& top_periods();
// Index into top_periods(), or npos.
std::size_t top_period_index(const std::string& name);
std::size_t next_period(std::size_t i, int step);

// --- view state ----------------------------------------------------------------

enum class PanelId { Apps = 0, Hosts = 1, Types = 2 };
constexpr std::size_t kPanels = 3;

struct TopState {
    std::size_t focus = 0;             // a PanelId
    std::size_t period = 2;            // into top_periods(): 24h
    core::RateUnit rate = core::RateUnit::Bits;
    core::VolumeUnit volume = core::VolumeUnit::Decimal;
    std::array<std::size_t, kPanels> scroll{};   // first row shown, per panel
};

// What the caller does after a key: nothing, draw again, read the history
// again for a new period (and draw), or leave.
enum class Action { None, Redraw, Reload, Quit };

// page: how many rows the focused panel shows, for PageUp/PageDown. Scrolling
// past the end is clamped when the screen is laid out (clamp_scroll), since
// only that knows how many rows fit.
Action apply_key(TopState& s, const Key& k, std::size_t page);

// End/G scroll to "as far as possible"; clamp_scroll brings it back.
constexpr std::size_t kScrollEnd = std::size_t(-1) / 2;

}  // namespace bandsight::cli
