// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QString>

#include "theme.h"
#include "units.h"

namespace bandsight::gui {

// The display preferences - speed unit, volume unit, theme, traffic colours -
// and the one place
// that changes them. Every widget reads them at paint time (format_rate,
// format_volume, theme()), so most need only a repaint; the few that turn a
// preference into a stored string or style sheet (labels, tooltips, table
// cells) listen to changed() and rebuild it. Either way a change shows
// everywhere at once, with no restart and no tab to reopen.
//
// Stored in QSettings (this user's own config): units/rate and units/volume as
// the integers they have always been, appearance/theme as theme_key(),
// traffic/colours as traffic_key() and the custom pair as traffic/download and
// traffic/upload ("#rrggbb").
class Appearance : public QObject {
    Q_OBJECT
public:
    static Appearance& instance();

    // Reads QSettings and applies everything. Unknown or missing values fall
    // back to the defaults (bits, decimal, System, theme colours).
    void load();
    // Writes everything to QSettings.
    void save() const;

    void setRateUnit(RateUnit u);
    void setVolumeUnit(VolumeUnit u);
    void setTheme(ThemeId id);
    // custom is kept even while a preset is chosen, so going back to Custom
    // finds the pair the user made.
    void setTrafficColours(TrafficColours c, const TrafficPair& custom);

    RateUnit rateUnit() const { return rate_unit(); }
    VolumeUnit volumeUnit() const { return volume_unit(); }
    ThemeId themeId() const { return current_theme(); }
    TrafficColours trafficColours() const { return current_traffic_colours(); }
    TrafficPair customTraffic() const { return custom_traffic_colours(); }

signals:
    // After any preference changed. Emitted before the repaint is scheduled,
    // so a listener's rebuilt text is what the repaint shows.
    void changed();

private:
    Appearance() = default;
    void applyTheme();
    void notify();

    // The style the desktop gave the app, restored when going back to System.
    QString system_style_;
    bool forced_ = false;   // a named theme's palette is installed
};

}  // namespace bandsight::gui
