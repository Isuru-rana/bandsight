// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QColor>

namespace bandsight::gui {

// The measurements the palettes in theme.h are held to. Shared by the
// contrast test and by the settings dialog, which warns about a custom
// download/upload colour with exactly the numbers the test would apply.

// WCAG 2.x contrast ratio, 1 to 21.
double contrast_ratio(const QColor& a, const QColor& b);

// Colour vision to simulate before comparing: none, or a full-severity
// deficiency (Machado, Oliveira & Fernandes 2009).
enum class Vision { Normal, Protan, Deutan, Tritan };

// OKLab ΔE x100 between two colours as seen with the given vision. The scale
// of the figures quoted in theme.h; 15 is the floor for "tell apart".
double delta_e(const QColor& a, const QColor& b, Vision v);

// The floors, in one place.
constexpr double kMinTextContrast = 4.5;
constexpr double kMinMarkContrast = 3.0;
constexpr double kMinDeltaE = 15.0;

}  // namespace bandsight::gui
