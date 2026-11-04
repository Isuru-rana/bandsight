// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QDialog>

#include "about.h"
#include "quota.h"
#include "theme.h"
#include "units.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;
class QTabWidget;

namespace bandsight::gui {

class AboutPage;

// Spec §7's Settings, in three sections: General, Display and About. Two
// different destinations, and the split is the point:
//
//   cap size + cycle start day -> the DAEMON, via set_config on the socket
//     (General). They describe the machine's connection, not this user's
//     taste, and the daemon owns the database.
//   units, theme, autostart    -> QSettings, this user's own config.
//     Purely how this window presents itself.
//
// Display choices apply the moment they are picked, everywhere, so the user
// sees the result before deciding; OK keeps them and Cancel puts back what was
// there when the dialog opened.
//
// Excluded interfaces are deliberately NOT duplicated here: they are the
// checkboxes on the Interfaces tab, next to the rows they apply to.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(const QuotaInfo& current, const DaemonInfo& daemon = {},
                            QWidget* parent = nullptr);

    // What the user chose. Read by MainWindow once the dialog is accepted.
    quint64 capBytes() const;
    int cycleDay() const;
    // Whether the cap or the cycle day differ from what the dialog opened
    // with: nothing is sent to the daemon for a dialog that only changed the
    // display.
    bool daemonSettingsChanged() const;
    RateUnit rateUnit() const;
    VolumeUnit volumeUnit() const;
    ThemeId themeId() const;
    bool autostart() const;

    void setDaemonInfo(const DaemonInfo& d);

    // Test-only, same rationale as the other widgets here.
    QDoubleSpinBox* capSpin() const { return cap_; }
    QSpinBox* daySpin() const { return day_; }
    QCheckBox* autostartBox() const { return autostart_; }
    QComboBox* rateCombo() const { return rate_; }
    QComboBox* volumeCombo() const { return volume_; }
    QComboBox* themeCombo() const { return theme_; }
    QTabWidget* sections() const { return tabs_; }
    AboutPage* aboutPage() const { return about_; }

    // Reports the daemon's answer to the write. Not a message box: the dialog
    // is already closed by then, and the reason belongs where the value was
    // typed rather than in a modal the user has to dismiss.
    void showResult(bool ok, const QString& message);

    // Keeps the local half (units and theme to QSettings, the autostart
    // file). The daemon half is MainWindow's, because only it owns the socket.
    void applyLocal() const;

    // Cancel, Escape and the window's close button: the display goes back to
    // what it was when the dialog opened.
    void reject() override;

    // Writes or removes ~/.config/autostart/bandsight.desktop. Static and public
    // so it is testable without showing a dialog.
    static bool writeAutostart(bool enabled, const QString& dir);
    static QString autostartDir();

private:
    QWidget* generalPage();
    QWidget* displayPage();

    QDoubleSpinBox* cap_;
    QSpinBox* day_;
    QComboBox* rate_;
    QComboBox* volume_;
    QComboBox* theme_;
    QCheckBox* autostart_;
    QLabel* result_;
    QTabWidget* tabs_;
    AboutPage* about_;

    // What was in effect when the dialog opened.
    quint64 initial_cap_;
    double initial_cap_shown_ = 0;
    int initial_day_;
    RateUnit initial_rate_;
    VolumeUnit initial_volume_;
    ThemeId initial_theme_;
};

}  // namespace bandsight::gui
