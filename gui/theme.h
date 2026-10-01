// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QColor>
#include <QVector>
#include <QtGlobal>

namespace bandsight::gui {

// Every colour and metric the custom widgets use, in one place.
//
// Spec §7 originally said "no custom styling - inherits Breeze and the system
// palette". That is amended deliberately (see the spec's own note): the Usage
// screen draws charts, and a chart's colours carry meaning rather than taste, so
// they cannot be left to whatever the desktop theme happens to pick. Everything
// NOT carrying meaning still comes from the system palette, so the window still
// belongs to the desktop it runs on.
//
// Dark is a SELECTED set of steps validated against the dark surface, not an
// automatic inversion of the light one - a flipped palette lands outside the
// lightness band and loses contrast against the surface.
//
// The download/upload pair is categorical (two identities, not a magnitude), is
// assigned in fixed order, and was validated rather than eyeballed: worst
// adjacent CVD ΔE 24.7 protan on light, 26.8 on dark, all six checks passing in
// both modes.
struct Theme {
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
    // recessive so a column of them does not out-shout the bars beside it.
    QVector<QColor> chips;
    QColor chip_ink;

    // The traffic chart's plot area. A faint cool tint rather than the card
    // white, so the plot reads as a surface of its own, with alternating time
    // bands a step darker. Both stay far below the data colours in contrast:
    // they organise the eye, they carry nothing.
    QColor plot_bg;
    QColor plot_band;     // the letter on a chip: contrast-checked against every chip

    // Marks are thin and data-ends are rounded (4px), which is what stops a bar
    // chart reading as a stack of blocks.
    static constexpr int radius_card = 8;
    static constexpr int radius_bar = 3;
    static constexpr int gap = 2;       // surface gap between adjacent fills
};

// Chosen from the current palette each time it is asked, so following the
// desktop into dark mode needs no restart and no signal.
Theme theme();

bool dark_mode();

}  // namespace bandsight::gui
