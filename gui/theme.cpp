// SPDX-License-Identifier: GPL-3.0-or-later
#include "theme.h"

#include <QApplication>
#include <QCoreApplication>

#include <algorithm>

#include "colour_checks.h"

namespace bandsight::gui {

namespace {

ThemeId g_theme = ThemeId::System;
TrafficColours g_traffic = TrafficColours::Theme;
// The default custom pair is the Light theme's own, so a first pick of Custom
// starts from something sensible rather than black on black.
TrafficPair g_custom{QColor(0x2a, 0x78, 0xd6), QColor(0xe5, 0x60, 0x2b)};

// The desktop's own lightness. Only meaningful while the System theme is
// active: a named theme replaces the application palette, and reading it back
// would only report the theme's own choice.
bool desktop_is_dark() {
    // The window colour, not a setting: it is what the widgets are actually
    // drawn against, and it is correct under every platform theme including the
    // ones that offer no dark/light flag at all.
    return QApplication::palette().color(QPalette::Window).lightness() < 128;
}

Theme light() {
    Theme t;
    t.dark      = false;
    t.surface   = QColor(0xfc, 0xfc, 0xfb);
    t.card      = QColor(0xff, 0xff, 0xff);
    t.border    = QColor(0xe2, 0xe2, 0xde);
    t.ink       = QColor(0x1c, 0x1c, 0x1a);
    t.ink_muted = QColor(0x6b, 0x6b, 0x66);
    t.ink_faint = QColor(0xa8, 0xa8, 0xa2);
    t.download  = QColor(0x2a, 0x78, 0xd6);   // slot 1, light step
    // Slot 2, light step. One step darker than the original #eb6834, which
    // measured 2.92:1 against plot_bg; this is 3.17:1, the hue unchanged.
    t.upload    = QColor(0xe5, 0x60, 0x2b);
    t.track     = QColor(0xed, 0xed, 0xe9);
    // Pale tints on the light surface, dark letter - the mirror choice, not
    // an inversion of the dark values.
    t.chips = {QColor(0xcc, 0xe3, 0xdf), QColor(0xdc, 0xd6, 0xec),
               QColor(0xf0, 0xd6, 0xe0), QColor(0xd8, 0xe6, 0xd0),
               QColor(0xe8, 0xe4, 0xc9), QColor(0xeb, 0xd6, 0xeb)};
    t.chip_ink  = QColor(0x2a, 0x2a, 0x28);
    t.plot_bg   = QColor(0xee, 0xf6, 0xf6);
    t.plot_band = QColor(0xe4, 0xef, 0xef);
    t.highlight     = QColor(0x24, 0x68, 0xbd);
    t.highlight_ink = QColor(0xff, 0xff, 0xff);
    t.link          = QColor(0x24, 0x68, 0xbd);
    return t;
}

Theme dark() {
    Theme t;
    t.dark      = true;
    t.surface   = QColor(0x1a, 0x1a, 0x19);
    t.card      = QColor(0x24, 0x24, 0x23);
    t.border    = QColor(0x3a, 0x3a, 0x38);
    t.ink       = QColor(0xf0, 0xf0, 0xee);
    t.ink_muted = QColor(0x9a, 0x9a, 0x96);
    t.ink_faint = QColor(0x5e, 0x5e, 0x5a);
    t.download  = QColor(0x39, 0x87, 0xe5);   // slot 1, dark step
    t.upload    = QColor(0xd9, 0x59, 0x26);   // slot 2, dark step
    t.track     = QColor(0x33, 0x33, 0x31);
    // Deep tints on the dark surface, light letter.
    t.chips = {QColor(0x2f, 0x5d, 0x57), QColor(0x4a, 0x3f, 0x66),
               QColor(0x63, 0x3b, 0x4e), QColor(0x3d, 0x53, 0x33),
               QColor(0x4b, 0x47, 0x33), QColor(0x66, 0x42, 0x66)};
    t.chip_ink  = QColor(0xec, 0xec, 0xe8);
    t.plot_bg   = QColor(0x1f, 0x26, 0x28);
    t.plot_band = QColor(0x24, 0x2c, 0x2e);
    t.highlight     = QColor(0x2c, 0x6a, 0xb8);
    t.highlight_ink = QColor(0xff, 0xff, 0xff);
    t.link          = QColor(0x5c, 0x9d, 0xed);
    return t;
}

// Black surfaces, white text, and data colours lifted well clear of both: for
// low vision and bright rooms. Borders are visible lines here, not hairlines.
Theme high_contrast() {
    Theme t;
    t.dark      = true;
    t.surface   = QColor(0x00, 0x00, 0x00);
    t.card      = QColor(0x0e, 0x0e, 0x0e);
    t.border    = QColor(0x8a, 0x8a, 0x8a);
    t.ink       = QColor(0xff, 0xff, 0xff);
    t.ink_muted = QColor(0xc8, 0xc8, 0xc8);
    t.ink_faint = QColor(0x80, 0x80, 0x80);
    t.download  = QColor(0x4d, 0xa3, 0xff);
    t.upload    = QColor(0xff, 0x8a, 0x3d);
    t.track     = QColor(0x2e, 0x2e, 0x2e);
    t.chips = {QColor(0x1f, 0x4d, 0x47), QColor(0x3b, 0x2f, 0x5e),
               QColor(0x5a, 0x2a, 0x40), QColor(0x2f, 0x4a, 0x24),
               QColor(0x4a, 0x44, 0x20), QColor(0x5a, 0x30, 0x5a)};
    t.chip_ink  = QColor(0xff, 0xff, 0xff);
    t.plot_bg   = QColor(0x07, 0x12, 0x14);
    t.plot_band = QColor(0x0f, 0x1e, 0x21);
    t.highlight     = QColor(0x4d, 0xa3, 0xff);
    t.highlight_ink = QColor(0x00, 0x00, 0x00);
    t.link          = QColor(0x4d, 0xa3, 0xff);
    return t;
}

// A warm light theme: paper-coloured surfaces and brown-black ink instead of
// neutral white. The data pair is darkened to hold 3:1 on the cream plot, and
// the chips are warm pale tints on the same principle as Light's.
Theme sand() {
    Theme t;
    t.dark      = false;
    t.surface   = QColor(0xf6, 0xf1, 0xe7);
    t.card      = QColor(0xff, 0xfb, 0xf3);
    t.border    = QColor(0xe3, 0xd9, 0xc6);
    t.ink       = QColor(0x2b, 0x24, 0x1b);
    t.ink_muted = QColor(0x6a, 0x5e, 0x4e);
    t.ink_faint = QColor(0xb0, 0xa5, 0x90);
    t.download  = QColor(0x26, 0x67, 0xb8);
    t.upload    = QColor(0xc4, 0x50, 0x1b);
    t.track     = QColor(0xee, 0xe6, 0xd6);
    t.chips = {QColor(0xd6, 0xe4, 0xd9), QColor(0xe2, 0xdb, 0xe9),
               QColor(0xf1, 0xd9, 0xd6), QColor(0xdf, 0xe6, 0xcf),
               QColor(0xec, 0xe2, 0xc4), QColor(0xea, 0xd6, 0xe4)};
    t.chip_ink  = QColor(0x2b, 0x24, 0x1b);
    t.plot_bg   = QColor(0xf3, 0xed, 0xe1);
    t.plot_band = QColor(0xec, 0xe4, 0xd4);
    t.highlight     = QColor(0x26, 0x67, 0xb8);
    t.highlight_ink = QColor(0xff, 0xff, 0xff);
    t.link          = QColor(0x26, 0x67, 0xb8);
    return t;
}

// A blue-grey dark theme: cooler and a little lighter than Dark, for a desk
// lamp rather than a dark room. The data blue is lifted so it still separates
// from a surface that is itself bluish.
Theme slate() {
    Theme t;
    t.dark      = true;
    t.surface   = QColor(0x1b, 0x21, 0x29);
    t.card      = QColor(0x23, 0x2b, 0x35);
    t.border    = QColor(0x36, 0x41, 0x4e);
    t.ink       = QColor(0xe8, 0xed, 0xf2);
    t.ink_muted = QColor(0x9b, 0xa8, 0xb6);
    t.ink_faint = QColor(0x5b, 0x67, 0x75);
    t.download  = QColor(0x5a, 0xa2, 0xf0);
    t.upload    = QColor(0xec, 0x7d, 0x44);
    t.track     = QColor(0x31, 0x3b, 0x47);
    t.chips = {QColor(0x2d, 0x54, 0x52), QColor(0x43, 0x3d, 0x63),
               QColor(0x5b, 0x3a, 0x4d), QColor(0x3a, 0x4f, 0x34),
               QColor(0x4b, 0x47, 0x34), QColor(0x5c, 0x40, 0x61)};
    t.chip_ink  = QColor(0xee, 0xf2, 0xf6);
    t.plot_bg   = QColor(0x1d, 0x26, 0x30);
    t.plot_band = QColor(0x22, 0x2c, 0x37);
    t.highlight     = QColor(0x2f, 0x6f, 0xb8);
    t.highlight_ink = QColor(0xff, 0xff, 0xff);
    t.link          = QColor(0x5a, 0xa2, 0xf0);
    return t;
}

}  // namespace

const QVector<ThemeId>& all_themes() {
    static const QVector<ThemeId> all{ThemeId::System, ThemeId::Light, ThemeId::Dark,
                                      ThemeId::HighContrast, ThemeId::Sand, ThemeId::Slate};
    return all;
}

QString theme_key(ThemeId id) {
    switch (id) {
    case ThemeId::System:       return QStringLiteral("system");
    case ThemeId::Light:        return QStringLiteral("light");
    case ThemeId::Dark:         return QStringLiteral("dark");
    case ThemeId::HighContrast: return QStringLiteral("high-contrast");
    case ThemeId::Sand:         return QStringLiteral("sand");
    case ThemeId::Slate:        return QStringLiteral("slate");
    }
    return QStringLiteral("system");
}

std::optional<ThemeId> theme_from_key(const QString& key) {
    for (ThemeId id : all_themes())
        if (theme_key(id) == key) return id;
    return std::nullopt;
}

QString theme_name(ThemeId id) {
    switch (id) {
    case ThemeId::System:       return QCoreApplication::translate("Theme", "System");
    case ThemeId::Light:        return QCoreApplication::translate("Theme", "Light");
    case ThemeId::Dark:         return QCoreApplication::translate("Theme", "Dark");
    case ThemeId::HighContrast: return QCoreApplication::translate("Theme", "High contrast");
    case ThemeId::Sand:         return QCoreApplication::translate("Theme", "Sand");
    case ThemeId::Slate:        return QCoreApplication::translate("Theme", "Slate");
    }
    return {};
}

Theme theme_palette(ThemeId id) {
    switch (id) {
    case ThemeId::System:       return desktop_is_dark() ? dark() : light();
    case ThemeId::Light:        return light();
    case ThemeId::Dark:         return dark();
    case ThemeId::HighContrast: return high_contrast();
    case ThemeId::Sand:         return sand();
    case ThemeId::Slate:        return slate();
    }
    return light();
}

void set_current_theme(ThemeId id) { g_theme = id; }
ThemeId current_theme() { return g_theme; }

Theme theme() { return with_traffic(theme_palette(g_theme), g_traffic, g_custom); }

bool dark_mode() { return theme().dark; }

QPalette qt_palette(const Theme& t) {
    // Button and window seed the derived shades (light, mid, dark, shadow) that
    // Fusion draws bevels and frames with; the roles below then pin everything
    // that carries text to the theme's own checked pairs.
    QPalette p(t.card, t.surface);
    p.setColor(QPalette::Window, t.surface);
    p.setColor(QPalette::WindowText, t.ink);
    p.setColor(QPalette::Base, t.card);
    p.setColor(QPalette::AlternateBase, t.plot_bg);
    p.setColor(QPalette::Text, t.ink);
    p.setColor(QPalette::Button, t.card);
    p.setColor(QPalette::ButtonText, t.ink);
    p.setColor(QPalette::BrightText, t.dark ? QColor(Qt::white) : QColor(Qt::black));
    p.setColor(QPalette::ToolTipBase, t.card);
    p.setColor(QPalette::ToolTipText, t.ink);
    p.setColor(QPalette::PlaceholderText, t.ink_muted);
    p.setColor(QPalette::Highlight, t.highlight);
    p.setColor(QPalette::HighlightedText, t.highlight_ink);
    p.setColor(QPalette::Link, t.link);
    p.setColor(QPalette::LinkVisited, t.link);
    p.setColor(QPalette::Mid, t.border);
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, t.ink_faint);
    p.setColor(QPalette::Disabled, QPalette::Highlight, t.track);
    return p;
}

// --- Traffic colours ----------------------------------------------------------

const QVector<TrafficColours>& all_traffic_colours() {
    static const QVector<TrafficColours> all{
        TrafficColours::Theme,       TrafficColours::BlueOrange, TrafficColours::TealMagenta,
        TrafficColours::GreenPurple, TrafficColours::BlueYellow, TrafficColours::Custom};
    return all;
}

QString traffic_key(TrafficColours c) {
    switch (c) {
    case TrafficColours::Theme:       return QStringLiteral("theme");
    case TrafficColours::BlueOrange:  return QStringLiteral("blue-orange");
    case TrafficColours::TealMagenta: return QStringLiteral("teal-magenta");
    case TrafficColours::GreenPurple: return QStringLiteral("green-purple");
    case TrafficColours::BlueYellow:  return QStringLiteral("blue-yellow");
    case TrafficColours::Custom:      return QStringLiteral("custom");
    }
    return QStringLiteral("theme");
}

std::optional<TrafficColours> traffic_from_key(const QString& key) {
    for (TrafficColours c : all_traffic_colours())
        if (traffic_key(c) == key) return c;
    return std::nullopt;
}

QString traffic_name(TrafficColours c) {
    const char* ctx = "TrafficColours";
    switch (c) {
    case TrafficColours::Theme:       return QCoreApplication::translate(ctx, "Default (follow theme)");
    case TrafficColours::BlueOrange:  return QCoreApplication::translate(ctx, "Blue / Orange");
    case TrafficColours::TealMagenta: return QCoreApplication::translate(ctx, "Teal / Magenta");
    case TrafficColours::GreenPurple: return QCoreApplication::translate(ctx, "Green / Purple");
    case TrafficColours::BlueYellow:  return QCoreApplication::translate(ctx, "Blue / Yellow");
    case TrafficColours::Custom:      return QCoreApplication::translate(ctx, "Custom");
    }
    return {};
}

// Light steps clear 3:1 on Light's and Sand's card and plot; dark steps on
// Dark's, High contrast's and Slate's. The figures are in the test's output.
std::optional<TrafficPair> traffic_preset(TrafficColours c, bool dark) {
    const auto pair = [](QRgb d, QRgb u) { return TrafficPair{QColor(d), QColor(u)}; };
    switch (c) {
    case TrafficColours::BlueOrange:
        // The pair the themes use. The light step is one shade darker than
        // Light's own: that one is 2.98:1 on Sand's plot.
        return dark ? pair(0x3987e5, 0xd95926) : pair(0x2a72cc, 0xd4561f);
    case TrafficColours::TealMagenta:
        // Red-green deficiency pulls teal and magenta towards the same grey,
        // so the pair is held apart by lightness: a deep plum against teal on
        // light surfaces, a bright aqua against magenta on dark ones.
        return dark ? pair(0x5ee0d1, 0xcc3399) : pair(0x1b9388, 0x7d0d5b);
    case TrafficColours::GreenPurple:
        return dark ? pair(0x4cb860, 0xa983ec) : pair(0x2e8540, 0x7b4fc4);
    case TrafficColours::BlueYellow:
        // Blue against yellow survives protan and deutan best of all. Yellow
        // cannot reach 3:1 on a light surface, so the light step is ochre.
        return dark ? pair(0x4d8ff0, 0xe8c03a) : pair(0x2a62c9, 0xa67600);
    case TrafficColours::Theme:
    case TrafficColours::Custom:
        return std::nullopt;
    }
    return std::nullopt;
}

void set_traffic_colours(TrafficColours c, const TrafficPair& custom) {
    g_traffic = c;
    if (custom.download.isValid() && custom.upload.isValid()) g_custom = custom;
}
TrafficColours current_traffic_colours() { return g_traffic; }
TrafficPair custom_traffic_colours() { return g_custom; }

Theme with_traffic(Theme t, TrafficColours c, const TrafficPair& custom) {
    if (c == TrafficColours::Custom) {
        if (custom.download.isValid()) t.download = custom.download;
        if (custom.upload.isValid()) t.upload = custom.upload;
    } else if (const auto p = traffic_preset(c, t.dark)) {
        t.download = p->download;
        t.upload = p->upload;
    }
    return t;
}

QStringList traffic_warnings(const Theme& t, const TrafficPair& pair) {
    const char* ctx = "TrafficColours";
    QStringList out;
    const auto visible = [&](const QColor& c) {
        return std::min(contrast_ratio(c, t.plot_bg), contrast_ratio(c, t.card)) >=
               kMinMarkContrast;
    };
    if (!visible(pair.download))
        out << QCoreApplication::translate(ctx, "Download is hard to see on this background.");
    if (!visible(pair.upload))
        out << QCoreApplication::translate(ctx, "Upload is hard to see on this background.");
    if (delta_e(pair.download, pair.upload, Vision::Normal) < kMinDeltaE)
        out << QCoreApplication::translate(ctx, "Download and upload look too alike.");
    else if (delta_e(pair.download, pair.upload, Vision::Protan) < kMinDeltaE ||
             delta_e(pair.download, pair.upload, Vision::Deutan) < kMinDeltaE)
        out << QCoreApplication::translate(
            ctx, "Download and upload are hard to tell apart for colour-blind people.");
    return out;
}

}  // namespace bandsight::gui
