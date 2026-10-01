// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QColor>
#include <QPainter>
#include <QRect>
#include <QString>

namespace bandsight::gui {

// A small rounded square carrying an application's initial, for the Apps column.
//
// Not an icon lookup, deliberately. Resolving real icons from .desktop files was
// measured against this machine's own database first: ZERO of the twelve
// heaviest applications resolved one - daemons like ssh, postgres and dockerd
// have no desktop entry at all, and browsers' entries point at wrapper paths
// that do not match the running binary - while the matches that did exist were
// wrong (java showed tlauncher's icon, flatpak showed OrcaSlicer's). A column of
// blanks and mislabels is worse than no icons.
//
// A chip cannot be wrong. It says only "this row is a different application from
// that one", which is exactly what the eye needs while scanning a list.

// The letter(s) shown: one character, or two for a name that starts with a
// non-letter so the chip is never blank.
QString chip_text(const QString& name);

// Deterministic: the same application gets the same colour in every window, on
// every run, and after any re-sort. Hashed rather than assigned by position,
// because position changes with traffic.
QColor chip_color(const QString& name);

void paint_identity_chip(QPainter& p, const QRect& box, const QString& name);

}  // namespace bandsight::gui
