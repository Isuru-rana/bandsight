// SPDX-License-Identifier: GPL-3.0-or-later
#include "theme.h"

#include <QApplication>
#include <QPalette>

namespace bandsight::gui {

bool dark_mode() {
    // The window colour, not a setting: it is what the widgets are actually
    // drawn against, and it is correct under every platform theme including the
    // ones that offer no dark/light flag at all.
    return QApplication::palette().color(QPalette::Window).lightness() < 128;
}

Theme theme() {
    Theme t;
    if (dark_mode()) {
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
        t.chip_ink = QColor(0xec, 0xec, 0xe8);
        t.plot_bg   = QColor(0x1f, 0x26, 0x28);
        t.plot_band = QColor(0x24, 0x2c, 0x2e);
    } else {
        t.surface   = QColor(0xfc, 0xfc, 0xfb);
        t.card      = QColor(0xff, 0xff, 0xff);
        t.border    = QColor(0xe2, 0xe2, 0xde);
        t.ink       = QColor(0x1c, 0x1c, 0x1a);
        t.ink_muted = QColor(0x6b, 0x6b, 0x66);
        t.ink_faint = QColor(0xa8, 0xa8, 0xa2);
        t.download  = QColor(0x2a, 0x78, 0xd6);   // slot 1, light step
        t.upload    = QColor(0xeb, 0x68, 0x34);   // slot 2, light step
        t.track     = QColor(0xed, 0xed, 0xe9);
        // Pale tints on the light surface, dark letter - the mirror choice, not
        // an inversion of the values above.
        t.chips = {QColor(0xcc, 0xe3, 0xdf), QColor(0xdc, 0xd6, 0xec),
                   QColor(0xf0, 0xd6, 0xe0), QColor(0xd8, 0xe6, 0xd0),
                   QColor(0xe8, 0xe4, 0xc9), QColor(0xeb, 0xd6, 0xeb)};
        t.chip_ink = QColor(0x2a, 0x2a, 0x28);
        t.plot_bg   = QColor(0xee, 0xf6, 0xf6);
        t.plot_band = QColor(0xe4, 0xef, 0xef);
    }
    return t;
}

}  // namespace bandsight::gui
