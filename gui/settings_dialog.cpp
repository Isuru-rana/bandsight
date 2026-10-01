// SPDX-License-Identifier: GPL-3.0-or-later
#include "settings_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFormLayout>
#include <QLabel>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTextStream>
#include <QVBoxLayout>

namespace bandsight::gui {

namespace {
// Caps are quoted in GB by every ISP, so that is the unit the box takes,
// whatever the display preference is set to. Storing bytes and showing GB keeps
// the daemon's value exact - a cap typed as 500 is 500e9, not 499.99.
constexpr double kBytesPerGb = 1e9;
}  // namespace

SettingsDialog::SettingsDialog(const QuotaInfo& current, QWidget* parent)
    : QDialog(parent),
      cap_(new QDoubleSpinBox(this)),
      day_(new QSpinBox(this)),
      rate_(new QComboBox(this)),
      volume_(new QComboBox(this)),
      autostart_(new QCheckBox(tr("Start bandsight when I log in"), this)),
      result_(new QLabel(this)) {
    setWindowTitle(tr("Bandsight Settings"));
    setObjectName(QStringLiteral("settingsDialog"));

    cap_->setRange(0, 1'000'000);
    cap_->setDecimals(1);
    cap_->setSuffix(tr(" GB"));
    cap_->setSpecialValueText(tr("No cap"));   // 0 is not "a cap of nothing"
    cap_->setValue(double(current.cap) / kBytesPerGb);

    // 1-28, matching the daemon's own rule: a cycle starting on the 30th does
    // not exist in February. The spin box refuses it rather than letting the
    // daemon reject it after the fact.
    day_->setRange(1, 28);
    day_->setValue(current.cycle_day);

    rate_->addItem(tr("Bits per second (Mbit/s)"), int(RateUnit::Bits));
    rate_->addItem(tr("Bytes per second (MB/s)"), int(RateUnit::Bytes));
    rate_->setCurrentIndex(rate_unit() == RateUnit::Bits ? 0 : 1);

    volume_->addItem(tr("Decimal (GB, 1000³) - as ISPs quote"), int(VolumeUnit::Decimal));
    volume_->addItem(tr("Binary (GiB, 1024³)"), int(VolumeUnit::Binary));
    volume_->setCurrentIndex(volume_unit() == VolumeUnit::Decimal ? 0 : 1);

    autostart_->setChecked(
        QFile::exists(autostartDir() + "/bandsight.desktop"));

    result_->setObjectName(QStringLiteral("settingsResult"));   // findChild, for tests
    result_->setWordWrap(true);
    result_->hide();

    auto* form = new QFormLayout;
    form->addRow(tr("Monthly data cap"), cap_);
    form->addRow(tr("Cycle starts on day"), day_);
    form->addRow(tr("Speed shown as"), rate_);
    form->addRow(tr("Volume shown as"), volume_);
    form->addRow(QString(), autostart_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* box = new QVBoxLayout(this);
    box->addLayout(form);
    box->addWidget(result_);
    box->addWidget(buttons);
}

quint64 SettingsDialog::capBytes() const {
    return quint64(cap_->value() * kBytesPerGb);
}
int SettingsDialog::cycleDay() const { return day_->value(); }
RateUnit SettingsDialog::rateUnit() const {
    return RateUnit(rate_->currentData().toInt());
}
VolumeUnit SettingsDialog::volumeUnit() const {
    return VolumeUnit(volume_->currentData().toInt());
}
bool SettingsDialog::autostart() const { return autostart_->isChecked(); }

void SettingsDialog::showResult(bool ok, const QString& message) {
    result_->setText(ok ? tr("Saved.") : tr("Not saved: %1").arg(message));
    result_->setVisible(true);
}

QString SettingsDialog::autostartDir() {
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) +
           QStringLiteral("/autostart");
}

bool SettingsDialog::writeAutostart(bool enabled, const QString& dir) {
    const QString path = dir + QStringLiteral("/bandsight.desktop");
    if (!enabled) {
        // Absent means disabled. Writing Hidden=true would also work and leaves
        // a file behind that a user who looks will have to reason about.
        QFile::remove(path);
        return true;
    }
    if (!QDir().mkpath(dir)) return false;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return false;
    QTextStream out(&f);
    // The running binary's own path, not a build-time constant: this file is
    // written by whichever bandsight the user actually launched, and hardcoding
    // a prefix here would recreate the install-path mismatch that produced the
    // 203/EXEC loop on the daemon side.
    out << "[Desktop Entry]\n"
        << "Type=Application\n"
        << "Name=Bandsight\n"
        << "Comment=Network usage monitor\n"
        << "Exec=" << QCoreApplication::applicationFilePath() << "\n"
        << "Icon=network-wired\n"
        << "Terminal=false\n"
        << "X-GNOME-Autostart-enabled=true\n";
    return true;
}

void SettingsDialog::applyLocal() const {
    set_rate_unit(rateUnit());
    set_volume_unit(volumeUnit());

    QSettings s;
    s.setValue(QStringLiteral("units/rate"), int(rateUnit()));
    s.setValue(QStringLiteral("units/volume"), int(volumeUnit()));

    writeAutostart(autostart(), autostartDir());
}

}  // namespace bandsight::gui
