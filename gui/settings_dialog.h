// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QDialog>

#include "quota.h"
#include "units.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;

namespace bandsight::gui {

// Spec §7's Settings. Two different destinations, and the split is the point:
//
//   cap size + cycle start day -> the DAEMON, via set_config on the socket.
//     They describe the machine's connection, not this user's taste, and the
//     daemon owns the database.
//   units + autostart          -> QSettings, this user's own config.
//     Purely how this window presents itself.
//
// Excluded interfaces are deliberately NOT duplicated here: they are the
// checkboxes on the Interfaces tab, next to the rows they apply to.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(const QuotaInfo& current, QWidget* parent = nullptr);

    // What the user chose. Read by MainWindow after exec() returns Accepted.
    quint64 capBytes() const;
    int cycleDay() const;
    RateUnit rateUnit() const;
    VolumeUnit volumeUnit() const;
    bool autostart() const;

    // Test-only, same rationale as the other widgets here.
    QDoubleSpinBox* capSpin() const { return cap_; }
    QSpinBox* daySpin() const { return day_; }
    QCheckBox* autostartBox() const { return autostart_; }

    // Reports the daemon's answer to the write. Not a message box: the dialog
    // is already closed by then, and the reason belongs where the value was
    // typed rather than in a modal the user has to dismiss.
    void showResult(bool ok, const QString& message);

    // Applies the local half immediately (units, autostart file). The daemon
    // half is MainWindow's, because only it owns the socket.
    void applyLocal() const;

    // Writes or removes ~/.config/autostart/bandsight.desktop. Static and public
    // so it is testable without showing a dialog.
    static bool writeAutostart(bool enabled, const QString& dir);
    static QString autostartDir();

private:
    QDoubleSpinBox* cap_;
    QSpinBox* day_;
    QComboBox* rate_;
    QComboBox* volume_;
    QCheckBox* autostart_;
    QLabel* result_;
};

}  // namespace bandsight::gui
