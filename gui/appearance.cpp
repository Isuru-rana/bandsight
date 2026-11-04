// SPDX-License-Identifier: GPL-3.0-or-later
#include "appearance.h"

#include <QApplication>
#include <QSettings>
#include <QStyle>
#include <QWidget>

namespace bandsight::gui {

namespace {
const QString kRateKey = QStringLiteral("units/rate");
const QString kVolumeKey = QStringLiteral("units/volume");
const QString kThemeKey = QStringLiteral("appearance/theme");
}  // namespace

Appearance& Appearance::instance() {
    static Appearance a;
    return a;
}

void Appearance::load() {
    QSettings s;
    // Range-checked: a hand-edited or future value must not become an enum
    // value no switch knows.
    const int rate = s.value(kRateKey, int(RateUnit::Bits)).toInt();
    set_rate_unit(rate == int(RateUnit::Bytes) ? RateUnit::Bytes : RateUnit::Bits);
    const int vol = s.value(kVolumeKey, int(VolumeUnit::Decimal)).toInt();
    set_volume_unit(vol == int(VolumeUnit::Binary) ? VolumeUnit::Binary : VolumeUnit::Decimal);
    set_current_theme(theme_from_key(s.value(kThemeKey).toString()).value_or(ThemeId::System));
    applyTheme();
    notify();
}

void Appearance::save() const {
    QSettings s;
    s.setValue(kRateKey, int(rate_unit()));
    s.setValue(kVolumeKey, int(volume_unit()));
    s.setValue(kThemeKey, theme_key(current_theme()));
}

void Appearance::setRateUnit(RateUnit u) {
    if (u == rate_unit()) return;
    set_rate_unit(u);
    notify();
}

void Appearance::setVolumeUnit(VolumeUnit u) {
    if (u == volume_unit()) return;
    set_volume_unit(u);
    notify();
}

void Appearance::setTheme(ThemeId id) {
    if (id == current_theme()) return;
    set_current_theme(id);
    applyTheme();
    notify();
}

// System leaves the desktop's style and palette alone, which is what it always
// did. A named theme installs Fusion and a palette built from the theme, so the
// native widgets - dialogs, combo popups, menus, scroll bars, tooltips - are
// drawn in the same colours as the cards. Fusion because it draws everything
// from the palette; a platform style may take colours from elsewhere and leave
// a light scroll bar in a dark window.
void Appearance::applyTheme() {
    if (!qobject_cast<QApplication*>(QCoreApplication::instance())) return;
    const ThemeId id = current_theme();
    if (id == ThemeId::System) {
        if (!forced_) return;
        forced_ = false;
        if (!system_style_.isEmpty()) QApplication::setStyle(system_style_);
        // An empty palette resolves nothing, so the application palette falls
        // back to the platform's, and follows the desktop again.
        QApplication::setPalette(QPalette());
        return;
    }
    if (!forced_) {
        system_style_ = QApplication::style()->name();
        forced_ = true;
        if (system_style_.compare(QLatin1String("fusion"), Qt::CaseInsensitive) != 0)
            QApplication::setStyle(QStringLiteral("Fusion"));
    }
    QApplication::setPalette(qt_palette(theme_palette(id)));
}

void Appearance::notify() {
    emit changed();
    // The painted widgets read the preference in paintEvent, and a palette
    // change does not reach a widget whose own colours did not change - a unit
    // switch changes no colour at all.
    if (qobject_cast<QApplication*>(QCoreApplication::instance()))
        for (QWidget* w : QApplication::allWidgets()) w->update();
}

}  // namespace bandsight::gui
