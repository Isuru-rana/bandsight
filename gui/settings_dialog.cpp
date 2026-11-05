// SPDX-License-Identifier: GPL-3.0-or-later
#include "settings_dialog.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTextStream>
#include <QVBoxLayout>

#include "appearance.h"

namespace bandsight::gui {

namespace {
// Caps are quoted in GB by every ISP, so that is the unit the box takes,
// whatever the display preference is set to. Storing bytes and showing GB keeps
// the daemon's value exact - a cap typed as 500 is 500e9, not 499.99.
constexpr double kBytesPerGb = 1e9;

// A filled rounded square in the colour, outlined so a colour close to the
// button face still reads as a swatch.
QIcon swatch(const QColor& c, const QColor& outline) {
    QPixmap pm(32, 32);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(outline, 2));
    p.setBrush(c);
    p.drawRoundedRect(QRectF(2, 2, 28, 28), 5, 5);
    p.end();
    return QIcon(pm);
}
}  // namespace

SettingsDialog::SettingsDialog(const QuotaInfo& current, const DaemonInfo& daemon,
                               QWidget* parent)
    : QDialog(parent),
      cap_(new QDoubleSpinBox(this)),
      day_(new QSpinBox(this)),
      rate_(new QComboBox(this)),
      volume_(new QComboBox(this)),
      theme_(new QComboBox(this)),
      traffic_(new QComboBox(this)),
      down_button_(new QPushButton(tr("Download"), this)),
      up_button_(new QPushButton(tr("Upload"), this)),
      reset_traffic_(new QPushButton(tr("Reset to theme colours"), this)),
      traffic_warning_(new QLabel(this)),
      custom_(custom_traffic_colours()),
      autostart_(new QCheckBox(tr("Start bandsight when I log in"), this)),
      result_(new QLabel(this)),
      tabs_(new QTabWidget(this)),
      about_(new AboutPage(daemon, this)),
      initial_cap_(current.cap),
      initial_day_(current.cycle_day),
      initial_rate_(rate_unit()),
      initial_volume_(volume_unit()),
      initial_theme_(current_theme()),
      initial_traffic_(current_traffic_colours()),
      initial_custom_(custom_traffic_colours()) {
    setWindowTitle(tr("Bandsight Settings"));
    setObjectName(QStringLiteral("settingsDialog"));

    tabs_->setObjectName(QStringLiteral("settingsSections"));
    tabs_->setDocumentMode(true);
    tabs_->addTab(generalPage(), tr("General"));
    tabs_->addTab(displayPage(), tr("Display"));
    tabs_->addTab(about_, tr("About"));

    result_->setObjectName(QStringLiteral("settingsResult"));   // findChild, for tests
    result_->setWordWrap(true);
    result_->hide();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* box = new QVBoxLayout(this);
    box->addWidget(tabs_, 1);
    box->addWidget(result_);
    box->addWidget(buttons);
    resize(520, 420);
}

QWidget* SettingsDialog::generalPage() {
    auto* page = new QWidget(this);

    cap_->setRange(0, 1'000'000);
    cap_->setDecimals(1);
    cap_->setSuffix(tr(" GB"));
    cap_->setSpecialValueText(tr("No cap"));   // 0 is not "a cap of nothing"
    cap_->setValue(double(initial_cap_) / kBytesPerGb);
    initial_cap_shown_ = cap_->value();

    // 1-28, matching the daemon's own rule: a cycle starting on the 30th does
    // not exist in February. The spin box refuses it rather than letting the
    // daemon reject it after the fact.
    day_->setRange(1, 28);
    day_->setValue(initial_day_);

    autostart_->setChecked(
        QFile::exists(autostartDir() + "/bandsight.desktop"));

    auto* form = new QFormLayout(page);
    form->addRow(tr("Monthly data cap"), cap_);
    form->addRow(tr("Cycle starts on day"), day_);
    form->addRow(QString(), autostart_);
    return page;
}

QWidget* SettingsDialog::displayPage() {
    auto* page = new QWidget(this);

    // Spelled out: "Mb/s" and "MB/s" differ by one letter's case and by a
    // factor of eight, and each is the natural unit for a different number
    // (an ISP's plan, a browser's download bar). The stored value is the
    // RateUnit integer it has always been.
    rate_->setObjectName(QStringLiteral("rateUnit"));
    rate_->addItem(tr("Mb/s - megabits per second (ISP speeds)"), int(RateUnit::Bits));
    rate_->addItem(tr("MB/s - megabytes per second (download speeds)"), int(RateUnit::Bytes));
    rate_->setCurrentIndex(rate_->findData(int(initial_rate_)));

    volume_->setObjectName(QStringLiteral("volumeUnit"));
    volume_->addItem(tr("Decimal (GB, 1000³) - as ISPs quote"), int(VolumeUnit::Decimal));
    volume_->addItem(tr("Binary (GiB, 1024³)"), int(VolumeUnit::Binary));
    volume_->setCurrentIndex(volume_->findData(int(initial_volume_)));

    theme_->setObjectName(QStringLiteral("theme"));
    for (ThemeId id : all_themes()) theme_->addItem(theme_name(id), theme_key(id));
    theme_->setItemData(0, tr("Follows the desktop's light or dark mode"), Qt::ToolTipRole);
    theme_->setCurrentIndex(theme_->findData(theme_key(initial_theme_)));

    // Applied on pick, not on OK: the point of a preview is seeing it.
    connect(rate_, &QComboBox::currentIndexChanged, this,
            [this] { Appearance::instance().setRateUnit(rateUnit()); });
    connect(volume_, &QComboBox::currentIndexChanged, this,
            [this] { Appearance::instance().setVolumeUnit(volumeUnit()); });
    connect(theme_, &QComboBox::currentIndexChanged, this,
            [this] { Appearance::instance().setTheme(themeId()); });

    // Traffic colours: the download/upload pair on top of whichever theme.
    traffic_->setObjectName(QStringLiteral("trafficColours"));
    for (TrafficColours c : all_traffic_colours()) traffic_->addItem(traffic_name(c), traffic_key(c));
    traffic_->setItemData(traffic_->findData(traffic_key(TrafficColours::BlueYellow)),
                          tr("Easiest to tell apart with red-green colour blindness"),
                          Qt::ToolTipRole);
    traffic_->setCurrentIndex(traffic_->findData(traffic_key(initial_traffic_)));
    connect(traffic_, &QComboBox::currentIndexChanged, this, [this] {
        Appearance::instance().setTrafficColours(trafficColours(), custom_);
        refreshTraffic();
    });

    down_button_->setObjectName(QStringLiteral("downloadColour"));
    up_button_->setObjectName(QStringLiteral("uploadColour"));
    down_button_->setToolTip(tr("Choose a custom download colour"));
    up_button_->setToolTip(tr("Choose a custom upload colour"));
    connect(down_button_, &QPushButton::clicked, this, [this] { pickColour(true); });
    connect(up_button_, &QPushButton::clicked, this, [this] { pickColour(false); });
    reset_traffic_->setObjectName(QStringLiteral("resetTrafficColours"));
    connect(reset_traffic_, &QPushButton::clicked, this, [this] {
        traffic_->setCurrentIndex(traffic_->findData(traffic_key(TrafficColours::Theme)));
    });

    traffic_warning_->setObjectName(QStringLiteral("trafficWarning"));
    traffic_warning_->setWordWrap(true);
    traffic_warning_->hide();

    auto* swatches = new QHBoxLayout;
    swatches->setContentsMargins(0, 0, 0, 0);
    swatches->addWidget(down_button_);
    swatches->addWidget(up_button_);
    swatches->addStretch();
    swatches->addWidget(reset_traffic_);

    auto* form = new QFormLayout(page);
    form->addRow(tr("Speed shown as"), rate_);
    form->addRow(tr("Volume shown as"), volume_);
    form->addRow(tr("Theme"), theme_);
    form->addRow(tr("Traffic colours"), traffic_);
    form->addRow(QString(), swatches);
    form->addRow(QString(), traffic_warning_);

    connect(&Appearance::instance(), &Appearance::changed, this, &SettingsDialog::refreshTraffic);
    refreshTraffic();
    return page;
}

void SettingsDialog::refreshTraffic() {
    const Theme t = theme();   // the pair in effect, whatever chose it
    down_button_->setIcon(swatch(t.download, t.border));
    up_button_->setIcon(swatch(t.upload, t.border));
    reset_traffic_->setEnabled(trafficColours() != TrafficColours::Theme);
    // Presets and the theme's own pair pass these checks by test; only a
    // custom pick can fail them, but asking every time costs nothing and
    // keeps the dialog honest if that ever changes.
    const QStringList warnings = traffic_warnings(t, {t.download, t.upload});
    traffic_warning_->setText(warnings.join(QLatin1Char('\n')));
    traffic_warning_->setVisible(!warnings.isEmpty());
}

void SettingsDialog::pickColour(bool download) {
    const Theme t = theme();
    const QColor current = download ? t.download : t.upload;
    const QColor c = QColorDialog::getColor(
        current, this, download ? tr("Download colour") : tr("Upload colour"));
    if (!c.isValid()) return;   // cancelled
    // The other colour stays as it is on screen, so picking one of the two
    // never silently changes the other.
    setCustomColours(download ? c : t.download, download ? t.upload : c);
}

void SettingsDialog::setCustomColours(const QColor& download, const QColor& upload) {
    custom_ = {download, upload};
    {
        const QSignalBlocker block(traffic_);
        traffic_->setCurrentIndex(traffic_->findData(traffic_key(TrafficColours::Custom)));
    }
    Appearance::instance().setTrafficColours(TrafficColours::Custom, custom_);
    refreshTraffic();
}

quint64 SettingsDialog::capBytes() const {
    return quint64(cap_->value() * kBytesPerGb);
}
int SettingsDialog::cycleDay() const { return day_->value(); }
bool SettingsDialog::daemonSettingsChanged() const {
    // Against the box's own starting value rather than the byte count: a cap
    // the box rounds for display (499.95 GB shown as 500.0) is not a change
    // until the user edits it.
    return cap_->value() != initial_cap_shown_ || cycleDay() != initial_day_;
}
RateUnit SettingsDialog::rateUnit() const {
    return RateUnit(rate_->currentData().toInt());
}
VolumeUnit SettingsDialog::volumeUnit() const {
    return VolumeUnit(volume_->currentData().toInt());
}
TrafficColours SettingsDialog::trafficColours() const {
    return traffic_from_key(traffic_->currentData().toString()).value_or(TrafficColours::Theme);
}
ThemeId SettingsDialog::themeId() const {
    return theme_from_key(theme_->currentData().toString()).value_or(ThemeId::System);
}
bool SettingsDialog::autostart() const { return autostart_->isChecked(); }

void SettingsDialog::setDaemonInfo(const DaemonInfo& d) { about_->setDaemonInfo(d); }

void SettingsDialog::reject() {
    Appearance& a = Appearance::instance();
    a.setRateUnit(initial_rate_);
    a.setVolumeUnit(initial_volume_);
    a.setTheme(initial_theme_);
    a.setTrafficColours(initial_traffic_, initial_custom_);
    QDialog::reject();
}

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
    Appearance& a = Appearance::instance();
    a.setRateUnit(rateUnit());
    a.setVolumeUnit(volumeUnit());
    a.setTheme(themeId());
    a.setTrafficColours(trafficColours(), custom_);
    a.save();

    writeAutostart(autostart(), autostartDir());
}

}  // namespace bandsight::gui
