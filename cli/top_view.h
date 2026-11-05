// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "core/history_db.h"
#include "daemon_socket.h"
#include "top_input.h"

namespace bandsight::cli {

// What `bandsight-cli top` shows, gathered by its loop and drawn by
// render_top(). Nothing here touches a terminal, so the screen can be laid out
// into a fixed-size buffer and compared as text.

struct PanelRow {
    std::string label;
    std::uint64_t down = 0, up = 0;
    bool muted = false;   // Unattributed
};

struct Panel {
    std::vector<PanelRow> rows;
    std::string empty_text;   // shown when rows is empty
    std::string note;         // e.g. "recorded since ...", beside the title
};

// The live side, from the daemon's socket.
struct LiveView {
    DaemonSocket::State state = DaemonSocket::State::NotRunning;
    std::string version;   // empty when not reported
    std::string error;     // why, for NoAccess / NoAnswer
    bool have_rate = false;
    std::uint64_t down = 0, up = 0;   // bytes in the newest second
    // The newest kSparkSeconds seconds, oldest first.
    std::deque<std::pair<std::uint64_t, std::uint64_t>> spark;
};
constexpr std::size_t kSparkSeconds = 60;

// The history side, from the database, for the period in TopState.
struct HistoryView {
    bool ok = false;
    std::string error;          // why not, in plain words
    std::string period_label;   // "last 24 hours"
    std::optional<core::Quota> quota;
    std::array<Panel, kPanels> panels;
};

struct TopData {
    std::int64_t now = 0;   // the clock in the header
    LiveView live;
    HistoryView history;
};

// Adds one second of samples to the live view: the rate and the sparkline.
void add_sample(LiveView& v, const core::Sample& s);

// Reads the three panels and the cycle for top_periods()[period] at now.
HistoryView load_history(core::HistoryDb& db, std::size_t period, std::int64_t now);

// How the screen is divided at a size. Rows are counted from 0.
struct Layout {
    int cols = 0, rows = 0;
    bool too_small = false;
    bool spark = false;          // the sparkline is shown
    int panels_top = 0;          // first row of the panel area
    // Rows each panel's list has (not counting its title and column header);
    // 0: the panel is not shown at this size.
    std::array<int, kPanels> visible{};
};
constexpr int kMinCols = 50, kMinRows = 12;

Layout layout_for(int cols, int rows, std::size_t focus);

// Keeps every panel's scroll inside its rows at this layout.
void clamp_scroll(TopState& s, const TopData& d, const Layout& l);

// How the screen may decorate itself: colour (never with NO_COLOR or off a
// terminal) and unicode blocks (an ASCII fallback for a terminal that cannot
// show them).
struct Look {
    bool colour = false;
    bool unicode = true;
};

// The screen as `rows` lines of exactly `cols` cells each. With look.colour
// off they are plain text; on, they carry SGR escapes (which take no cells).
std::vector<std::string> render_top(const TopState& s, const TopData& d, const Layout& l,
                                    const Look& look);

}  // namespace bandsight::cli
