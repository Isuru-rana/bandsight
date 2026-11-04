// SPDX-License-Identifier: GPL-3.0-or-later
// Every named theme against the checks theme.h promises. A new theme that
// fails any of them fails here, with the pair and the measured value printed.
// Runs on Qt's offscreen platform (set by ctest).
#include <QApplication>
#include <QPalette>
#include <QSet>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include "check.h"
#include "theme.h"

using namespace bandsight::gui;

namespace {

// --- WCAG 2.x contrast ratio -------------------------------------------------
double linear(double c8) {
    const double c = c8 / 255.0;
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}
double encode(double v) {
    v = std::clamp(v, 0.0, 1.0);
    return 255.0 * (v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1 / 2.4) - 0.055);
}
double luminance(const QColor& c) {
    return 0.2126 * linear(c.red()) + 0.7152 * linear(c.green()) + 0.0722 * linear(c.blue());
}
double contrast(const QColor& a, const QColor& b) {
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

// --- Colour vision deficiency ------------------------------------------------
// Machado, Oliveira & Fernandes (2009), severity 1.0, applied to linear RGB.
using Mat = std::array<std::array<double, 3>, 3>;
constexpr Mat kProtan{{{0.152286, 1.052583, -0.204868},
                       {0.114503, 0.786281, 0.099216},
                       {-0.003882, -0.048116, 1.051998}}};
constexpr Mat kDeutan{{{0.367322, 0.860646, -0.227968},
                       {0.280085, 0.672501, 0.047413},
                       {-0.011820, 0.042940, 0.968881}}};
constexpr Mat kTritan{{{1.255528, -0.076749, -0.178779},
                       {-0.078411, 0.930809, 0.147602},
                       {0.004733, 0.691367, 0.303900}}};

std::array<double, 3> simulate(const QColor& c, const Mat* m) {
    const std::array<double, 3> v{linear(c.red()), linear(c.green()), linear(c.blue())};
    if (!m) return {double(c.red()), double(c.green()), double(c.blue())};
    std::array<double, 3> out{};
    for (int i = 0; i < 3; ++i)
        out[i] = encode((*m)[i][0] * v[0] + (*m)[i][1] * v[1] + (*m)[i][2] * v[2]);
    return out;
}

// OKLab, from 8-bit sRGB. ΔE is the Euclidean distance x100, which puts it on
// the same scale as the figures quoted in theme.h.
std::array<double, 3> oklab(const std::array<double, 3>& rgb8) {
    const double r = linear(rgb8[0]), g = linear(rgb8[1]), b = linear(rgb8[2]);
    const double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
    const double m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
    const double s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
    return {0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
            1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
            0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s};
}
double delta_e(const QColor& a, const QColor& b, const Mat* m) {
    const auto x = oklab(simulate(a, m)), y = oklab(simulate(b, m));
    return 100.0 * std::sqrt((x[0] - y[0]) * (x[0] - y[0]) + (x[1] - y[1]) * (x[1] - y[1]) +
                             (x[2] - y[2]) * (x[2] - y[2]));
}

// Prints every measured pair, so the table in the PR comes from this run.
void need(const char* theme, const char* what, double got, double floor) {
    std::printf("  %-14s %-30s %6.2f  (>= %.1f)%s\n", theme, what, got, floor,
                got >= floor ? "" : "  FAIL");
    CHECK(got >= floor);
}

void check_theme(ThemeId id) {
    const QByteArray key = theme_key(id).toUtf8();
    const char* n = key.constData();
    const Theme t = theme_palette(id);

    // Text: 4.5:1 (WCAG AA, normal text) on everything it is drawn on.
    for (auto [fg_name, fg] : {std::pair{"ink", t.ink}, std::pair{"ink_muted", t.ink_muted}})
        for (auto [bg_name, bg] : {std::pair{"card", t.card}, std::pair{"surface", t.surface},
                                   std::pair{"plot_bg", t.plot_bg}})
            need(n, QStringLiteral("%1 / %2").arg(fg_name, bg_name).toUtf8().constData(),
                 contrast(fg, bg), 4.5);

    // Data marks: 3:1 (WCAG non-text contrast) on the card and the plot.
    for (auto [fg_name, fg] : {std::pair{"download", t.download}, std::pair{"upload", t.upload}})
        for (auto [bg_name, bg] : {std::pair{"card", t.card}, std::pair{"plot_bg", t.plot_bg}})
            need(n, QStringLiteral("%1 / %2").arg(fg_name, bg_name).toUtf8().constData(),
                 contrast(fg, bg), 3.0);

    // Chips: the letter is what is read; the fill is a recessive tint.
    CHECK(!t.chips.isEmpty());
    double chip_min = 99;
    for (const QColor& c : t.chips) chip_min = std::min(chip_min, contrast(t.chip_ink, c));
    need(n, "chip_ink / every chip", chip_min, 4.5);

    need(n, "highlight_ink / highlight", contrast(t.highlight_ink, t.highlight), 4.5);
    need(n, "link / card", contrast(t.link, t.card), 4.5);
    need(n, "link / surface", contrast(t.link, t.surface), 4.5);

    // Download vs upload, for normal vision and each simulated deficiency.
    need(n, "dE download/upload normal", delta_e(t.download, t.upload, nullptr), 15);
    need(n, "dE download/upload protan", delta_e(t.download, t.upload, &kProtan), 15);
    need(n, "dE download/upload deutan", delta_e(t.download, t.upload, &kDeutan), 15);
    need(n, "dE download/upload tritan", delta_e(t.download, t.upload, &kTritan), 15);

    // The flag the widgets branch on agrees with the colours.
    CHECK(t.dark == (t.surface.lightness() < 128));

    // The Qt palette a named theme installs carries the checked pairs.
    const QPalette p = qt_palette(t);
    CHECK(p.color(QPalette::Window) == t.surface);
    CHECK(p.color(QPalette::WindowText) == t.ink);
    CHECK(p.color(QPalette::Base) == t.card);
    CHECK(p.color(QPalette::Text) == t.ink);
    CHECK(p.color(QPalette::ButtonText) == t.ink);
    CHECK(p.color(QPalette::Highlight) == t.highlight);
    CHECK(p.color(QPalette::HighlightedText) == t.highlight_ink);
    CHECK(p.color(QPalette::ToolTipText) == t.ink);
    CHECK(p.color(QPalette::ToolTipBase) == t.card);
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // The method reproduces the figures theme.h has always quoted for the
    // original pair, so the numbers below are on the same scale.
    CHECK(std::abs(delta_e(QColor(0x2a, 0x78, 0xd6), QColor(0xeb, 0x68, 0x34), &kProtan) -
                   24.7) < 0.05);
    CHECK(std::abs(delta_e(QColor(0x39, 0x87, 0xe5), QColor(0xd9, 0x59, 0x26), &kProtan) -
                   26.8) < 0.05);

    // The table: System first, keys unique and round-tripping, names unique.
    CHECK(all_themes().size() >= 6);
    CHECK(all_themes().first() == ThemeId::System);
    QSet<QString> keys, names;
    for (ThemeId id : all_themes()) {
        keys.insert(theme_key(id));
        names.insert(theme_name(id));
        CHECK(theme_from_key(theme_key(id)) == id);
    }
    CHECK(keys.size() == all_themes().size());
    CHECK(names.size() == all_themes().size());
    CHECK(!theme_from_key(QStringLiteral("no-such-theme")));
    CHECK(!theme_from_key(QString()));

    // Every named theme, light and dark alike.
    int dark = 0, light = 0;
    for (ThemeId id : all_themes()) {
        if (id == ThemeId::System) continue;   // resolves to Light or Dark
        check_theme(id);
        (theme_palette(id).dark ? dark : light)++;
    }
    CHECK(dark >= 2 && light >= 2);

    // System follows the desktop: Light on a light window colour, Dark on a
    // dark one, and dark_mode() with it.
    set_current_theme(ThemeId::System);
    QApplication::setPalette(QPalette(QColor(0xef, 0xef, 0xef), QColor(0xf6, 0xf6, 0xf6)));
    CHECK(!theme_palette(ThemeId::System).dark);
    CHECK(theme().card == theme_palette(ThemeId::Light).card);
    CHECK(!dark_mode());
    QApplication::setPalette(QPalette(QColor(0x30, 0x30, 0x30), QColor(0x20, 0x20, 0x20)));
    CHECK(theme_palette(ThemeId::System).dark);
    CHECK(theme().card == theme_palette(ThemeId::Dark).card);
    CHECK(dark_mode());

    // A named theme ignores the desktop.
    set_current_theme(ThemeId::Sand);
    CHECK(!dark_mode());
    CHECK(theme().surface == theme_palette(ThemeId::Sand).surface);
    set_current_theme(ThemeId::HighContrast);
    QApplication::setPalette(QPalette(QColor(0xef, 0xef, 0xef), QColor(0xf6, 0xf6, 0xf6)));
    CHECK(dark_mode());

    if (g_failures) std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    return g_failures ? 1 : 0;
}
