// SPDX-License-Identifier: GPL-3.0-or-later
// Every named theme against the checks theme.h promises. A new theme that
// fails any of them fails here, with the pair and the measured value printed.
// Runs on Qt's offscreen platform (set by ctest).
#include <QApplication>
#include <QPalette>
#include <QSet>

#include <cmath>
#include <cstdio>

#include "check.h"
#include "colour_checks.h"
#include "theme.h"

using namespace bandsight::gui;

namespace {

double contrast(const QColor& a, const QColor& b) { return contrast_ratio(a, b); }

// Prints every measured pair, so the table in the PR comes from this run.
void need(const char* theme, const char* what, double got, double floor) {
    std::printf("  %-28s %-30s %6.2f  (>= %.1f)%s\n", theme, what, got, floor,
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
    need(n, "dE download/upload normal", delta_e(t.download, t.upload, Vision::Normal), 15);
    need(n, "dE download/upload protan", delta_e(t.download, t.upload, Vision::Protan), 15);
    need(n, "dE download/upload deutan", delta_e(t.download, t.upload, Vision::Deutan), 15);
    need(n, "dE download/upload tritan", delta_e(t.download, t.upload, Vision::Tritan), 15);

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

// A traffic colour preset on a theme: the chosen pair, through the same
// with_traffic() the widgets get it from, held to the data-mark floors.
void check_preset(TrafficColours c, ThemeId id) {
    const QByteArray label = (traffic_key(c) + QStringLiteral(" on ") + theme_key(id)).toUtf8();
    const char* n = label.constData();
    const Theme t = with_traffic(theme_palette(id), c, {});
    const auto preset = traffic_preset(c, t.dark);
    CHECK(preset.has_value());
    if (!preset) return;
    CHECK(t.download == preset->download && t.upload == preset->upload);
    for (auto [fg_name, fg] : {std::pair{"download", t.download}, std::pair{"upload", t.upload}})
        for (auto [bg_name, bg] : {std::pair{"card", t.card}, std::pair{"plot_bg", t.plot_bg}})
            need(n, QStringLiteral("%1 / %2").arg(fg_name, bg_name).toUtf8().constData(),
                 contrast(fg, bg), 3.0);
    need(n, "dE normal", delta_e(t.download, t.upload, Vision::Normal), 15);
    need(n, "dE protan", delta_e(t.download, t.upload, Vision::Protan), 15);
    need(n, "dE deutan", delta_e(t.download, t.upload, Vision::Deutan), 15);
    // Reported, not required: blue/yellow is the classic tritan confusion.
    std::printf("  %-28s %-30s %6.2f  (reported)\n", n, "dE tritan",
                delta_e(t.download, t.upload, Vision::Tritan));
    // A pair the test passes must not raise the dialog's warning either.
    CHECK(traffic_warnings(t, {t.download, t.upload}).isEmpty());
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // The method reproduces the figures theme.h has always quoted for the
    // original pair, so the numbers below are on the same scale.
    CHECK(std::abs(delta_e(QColor(0x2a, 0x78, 0xd6), QColor(0xeb, 0x68, 0x34), Vision::Protan) -
                   24.7) < 0.05);
    CHECK(std::abs(delta_e(QColor(0x39, 0x87, 0xe5), QColor(0xd9, 0x59, 0x26), Vision::Protan) -
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

    // Every traffic colour preset on every named theme.
    for (TrafficColours c : all_traffic_colours()) {
        if (c == TrafficColours::Theme || c == TrafficColours::Custom) {
            CHECK(!traffic_preset(c, false) && !traffic_preset(c, true));
            continue;
        }
        for (ThemeId id : all_themes())
            if (id != ThemeId::System) check_preset(c, id);
    }
    // Default changes nothing; Custom uses the given pair as it is.
    for (ThemeId id : all_themes()) {
        const Theme base = theme_palette(id);
        const Theme same = with_traffic(base, TrafficColours::Theme, {Qt::red, Qt::green});
        CHECK(same.download == base.download && same.upload == base.upload);
        const Theme custom = with_traffic(base, TrafficColours::Custom, {Qt::red, Qt::green});
        CHECK(custom.download == QColor(Qt::red) && custom.upload == QColor(Qt::green));
    }
    QSet<QString> tkeys, tnames;
    for (TrafficColours c : all_traffic_colours()) {
        tkeys.insert(traffic_key(c));
        tnames.insert(traffic_name(c));
        CHECK(traffic_from_key(traffic_key(c)) == c);
    }
    CHECK(tkeys.size() == all_traffic_colours().size());
    CHECK(tnames.size() == all_traffic_colours().size());
    CHECK(all_traffic_colours().first() == TrafficColours::Theme);
    CHECK(all_traffic_colours().last() == TrafficColours::Custom);
    CHECK(traffic_name(TrafficColours::Theme) == QStringLiteral("Default (follow theme)"));

    // The warnings, in plain words.
    {
        const Theme light = theme_palette(ThemeId::Light);
        // Pale yellow on a white card: invisible.
        QStringList w = traffic_warnings(light, {light.download, QColor(0xff, 0xf0, 0x80)});
        CHECK(w == QStringList{QStringLiteral("Upload is hard to see on this background.")});
        w = traffic_warnings(light, {QColor(0xf0, 0xf0, 0xf0), light.upload});
        CHECK(w == QStringList{QStringLiteral("Download is hard to see on this background.")});
        // Red against green of the same lightness: fine for normal vision,
        // a single colour for protan and deutan.
        w = traffic_warnings(light, {QColor(0xc0, 0x40, 0x30), QColor(0x3f, 0x80, 0x20)});
        CHECK(w.contains(QStringLiteral(
            "Download and upload are hard to tell apart for colour-blind people.")));
        w = traffic_warnings(light, {QColor(0x2a, 0x78, 0xd6), QColor(0x2c, 0x7a, 0xd8)});
        CHECK(w.contains(QStringLiteral("Download and upload look too alike.")));
        CHECK(traffic_warnings(light, {light.download, light.upload}).isEmpty());
    }

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
