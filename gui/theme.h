// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QColor>
#include <QPalette>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

#include <optional>

namespace bandsight::gui {

// Every colour and metric the custom widgets use, in one place.
//
// Spec §7 originally said "no custom styling - inherits Breeze and the system
// palette". That is amended deliberately (see the spec's own note): the Usage
// screen draws charts, and a chart's colours carry meaning rather than taste, so
// they cannot be left to whatever the desktop theme happens to pick. Under the
// System theme everything NOT carrying meaning still comes from the system
// palette, so the window still belongs to the desktop it runs on.
//
// Each dark palette is a SELECTED set of steps validated against its own
// surface, not an automatic inversion of a light one - a flipped palette lands
// outside the lightness band and loses contrast against the surface.
//
// The download/upload pair is categorical (two identities, not a magnitude), is
// assigned in fixed order, and is validated rather than eyeballed: OKLab ΔE
// (x100) between the two after a full-severity Machado protan, deutan and tritan
// simulation, floor 15. Every palette in the table is held to the same checks
// by tests/test_theme.cpp, which loops over all of them:
//   - ink and ink_muted vs card, surface and plot_bg: >= 4.5:1
//   - download and upload vs card and plot_bg: >= 3:1
//   - chip_ink vs every chip: >= 4.5:1
//   - highlight_ink vs highlight: >= 4.5:1
//   - link vs card and surface: >= 4.5:1
//   - download vs upload: ΔE >= 15 for normal vision and each simulation
struct Theme {
    bool dark = false;   // drawn light-on-dark

    QColor surface;      // window behind the cards
    QColor card;         // panel fill
    QColor border;       // hairline around cards and axes
    QColor ink;          // primary text
    QColor ink_muted;    // secondary text, axis labels
    QColor ink_faint;    // reserved/disabled panels
    QColor download;     // categorical slot 1
    QColor upload;       // categorical slot 2
    QColor track;        // the unfilled part of a proportion bar

    // Identity chips for the Apps column. Decorative, not an encoding: they say
    // "this row is a different application from that one", never how much or in
    // which direction.
    //
    // Deliberately low-chroma and deliberately AWAY from the download/upload
    // hues. Blue and orange carry meaning everywhere else on this screen, and a
    // decorative chip in either would be read as one of them. They are also kept
    // recessive so a column of them does not out-shout the bars beside it, which
    // is why the contrast that is checked is the letter on the chip, not the
    // chip against the card.
    QVector<QColor> chips;
    QColor chip_ink;     // the letter on a chip: contrast-checked against every chip

    // The traffic chart's plot area. A faint tint rather than the card fill, so
    // the plot reads as a surface of its own, with alternating time bands a step
    // darker. Both stay far below the data colours in contrast: they organise
    // the eye, they carry nothing.
    QColor plot_bg;
    QColor plot_band;

    // Selection in native widgets (lists, combo popups, menus) when a named
    // theme sets the application palette. Not the download blue itself: that
    // step is tuned for 3:1 against the plot, and selected text needs 4.5:1.
    QColor highlight;
    QColor highlight_ink;
    QColor link;         // link text on card and surface (About)

    // Marks are thin and data-ends are rounded (4px), which is what stops a bar
    // chart reading as a stack of blocks.
    static constexpr int radius_card = 8;
    static constexpr int radius_bar = 3;
    static constexpr int gap = 2;       // surface gap between adjacent fills
};

// The theme setting. System follows the desktop's light or dark palette, as the
// app always has; every other entry is a fixed palette and also sets a matching
// Qt palette (Fusion style) so dialogs, menus and scroll bars agree with it.
enum class ThemeId { System, Light, Dark, HighContrast, Sand, Slate };

// Every setting in menu order, System first.
const QVector<ThemeId>& all_themes();
// Stable key stored in QSettings ("system", "light", ...). Never translated.
QString theme_key(ThemeId id);
std::optional<ThemeId> theme_from_key(const QString& key);
QString theme_name(ThemeId id);   // shown in the settings combo

// The fixed palette for a named theme. For System, the Light or Dark palette
// the desktop currently calls for.
Theme theme_palette(ThemeId id);

// The process-wide setting. Changing it is Appearance's job (appearance.h),
// which also re-styles the widgets; this only records it.
void set_current_theme(ThemeId id);
ThemeId current_theme();

// The active palette, with the traffic colour choice applied. Chosen each time
// it is asked, so following the desktop into dark mode under System needs no
// restart and no signal.
Theme theme();

// Is the active theme dark. Under System, read from the desktop palette.
bool dark_mode();

// The Qt palette a named theme installs for native widgets.
QPalette qt_palette(const Theme& t);

// --- Traffic colours ----------------------------------------------------------
// The download/upload pair, chosen apart from the theme. Whatever is chosen
// still lands in Theme::download and Theme::upload - the categorical slots 1
// and 2 every widget already draws from - so no widget knows this exists.
//
// Each preset is two validated pairs: a step for light surfaces and a step for
// dark ones, picked by the active theme's darkness, the same way theme.h picks
// a theme's own pair. tests/test_theme.cpp holds every preset against every
// theme: each colour >= 3:1 on card and plot_bg, and the pair >= ΔE 15 for
// normal vision, protan and deutan. Custom colours are the user's and are not
// checked here; the settings dialog warns about them instead.
enum class TrafficColours { Theme, BlueOrange, TealMagenta, GreenPurple, BlueYellow, Custom };

struct TrafficPair {
    QColor download;
    QColor upload;
};

// Menu order: Theme first, Custom last.
const QVector<TrafficColours>& all_traffic_colours();
QString traffic_key(TrafficColours c);   // stored in QSettings; never translated
std::optional<TrafficColours> traffic_from_key(const QString& key);
QString traffic_name(TrafficColours c);

// A preset's pair for a light or dark surface. Theme and Custom have no fixed
// pair and return nullopt.
std::optional<TrafficPair> traffic_preset(TrafficColours c, bool dark);

// The process-wide choice; Appearance changes it. custom is used only for
// Custom, and is kept while another choice is active so switching back finds it.
void set_traffic_colours(TrafficColours c, const TrafficPair& custom);
TrafficColours current_traffic_colours();
TrafficPair custom_traffic_colours();

// t with its download/upload replaced by the choice. Theme leaves t alone.
Theme with_traffic(Theme t, TrafficColours c, const TrafficPair& custom);

// Plain-words problems with a download/upload pair on a theme: either colour
// under 3:1 against the plot background or the card, or the pair under ΔE 15
// for normal vision or for red-green colour blindness. Empty when it is fine.
QStringList traffic_warnings(const Theme& t, const TrafficPair& pair);

}  // namespace bandsight::gui
