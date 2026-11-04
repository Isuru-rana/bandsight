// SPDX-License-Identifier: GPL-3.0-or-later
// The settings entry points, the Display section's live preview, theme
// switching, and the About section, on the real widgets.
// Runs on Qt's offscreen platform (set by ctest).
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QSettings>
#include <QStandardPaths>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QToolButton>

#include "about.h"
#include "appearance.h"
#include "check.h"
#include "live_client.h"
#include "main_window.h"
#include "models/iface_model.h"
#include "quota_bar.h"
#include "settings_dialog.h"
#include "tabs/usage_tab.h"
#include "theme.h"
#include "tray_icon.h"
#include "units.h"
#include "widgets/card.h"
#include "widgets/ranked_list.h"
#include "widgets/stat_pair.h"

using namespace bandsight::gui;

namespace {

QStringList tab_titles(QTabWidget* t) {
    QStringList out;
    for (int i = 0; i < t->count(); ++i) out << t->tabText(i);
    return out;
}

QStringList item_texts(QComboBox* c) {
    QStringList out;
    for (int i = 0; i < c->count(); ++i) out << c->itemText(i);
    return out;
}

AppHostTotal pair(const char* ip, quint64 rx) {
    AppHostTotal p;
    p.exe = QStringLiteral("/usr/bin/curl");
    p.app_name = QStringLiteral("curl");
    p.ip = QString::fromLatin1(ip);
    p.rx = rx;
    return p;
}

}  // namespace

int main(int argc, char** argv) {
    // QSettings and the autostart file go to a throwaway location, never the
    // user's real config.
    QStandardPaths::setTestModeEnabled(true);
    QApplication app(argc, argv);
    QSettings().clear();

    Appearance& a = Appearance::instance();
    a.load();   // defaults: bits, decimal, System
    CHECK(a.rateUnit() == RateUnit::Bits);
    CHECK(a.volumeUnit() == VolumeUnit::Decimal);
    CHECK(a.themeId() == ThemeId::System);
    const QString system_style = QApplication::style()->name();
    const QColor system_window = QApplication::palette().color(QPalette::Window);

    // --- version: from CMake, never a literal --------------------------------
    CHECK(app_version() == QStringLiteral(BANDSIGHT_TEST_PROJECT_VERSION));
    CHECK(about_title() == QStringLiteral("Bandsight " BANDSIGHT_TEST_PROJECT_VERSION));

    // --- a unit switch reaches live widgets without being re-fed --------------
    QuotaBar quota;
    QuotaInfo q;
    q.used = 3'000'000'000;
    q.cap = 0;
    quota.setQuota(q);
    CHECK(quota.text().startsWith(QStringLiteral("3.00 GB")));

    StatPair stats;
    stats.setValues(2'000'000'000, 0);
    CHECK(stats.downText() == QStringLiteral("2.00 GB"));

    IfaceModel model;
    IfaceTotal it;
    it.id = 2;
    it.name = QStringLiteral("eth0");
    it.kind = QStringLiteral("physical");
    it.rx = 5'000'000'000;
    model.setTotals({it});
    int data_changed = 0;
    QObject::connect(&model, &IfaceModel::dataChanged, [&] { ++data_changed; });
    CHECK(model.data(model.index(0, IfaceModel::Rx)).toString() == QStringLiteral("5.00 GB"));

    UsageTab usage;
    usage.setHostPairs({pair("93.184.216.34", 4'000'000'000)}, true);
    RankedList* hosts = usage.hosts();
    CHECK(hosts->rows().size() == 1);
    CHECK(hosts->rows().value(0).tooltip.contains(QStringLiteral("curl 4.00 GB")));

    QWidget tray_window;
    TrayIcon tray(&tray_window);
    SampleFrame f;
    f.ifaces = {{2, 1'500'000, 125'000}};
    tray.onSample(f);
    CHECK(tray.icon()->toolTip().contains(QStringLiteral("12.0 Mbit/s")));

    a.setVolumeUnit(VolumeUnit::Binary);
    CHECK(quota.text().startsWith(QStringLiteral("2.79 GiB")));
    CHECK(stats.downText() == QStringLiteral("1.86 GiB"));
    CHECK(data_changed == 1);
    CHECK(model.data(model.index(0, IfaceModel::Rx)).toString() == QStringLiteral("4.66 GiB"));
    CHECK(hosts->rows().value(0).tooltip.contains(QStringLiteral("curl 3.73 GiB")));

    a.setRateUnit(RateUnit::Bytes);
    CHECK(tray.icon()->toolTip().contains(QStringLiteral("1.5 MB/s")));
    CHECK(tray.icon()->toolTip().contains(QStringLiteral("125.0 kB/s")));
    a.setRateUnit(RateUnit::Bits);
    a.setVolumeUnit(VolumeUnit::Decimal);
    CHECK(quota.text().startsWith(QStringLiteral("3.00 GB")));
    CHECK(tray.icon()->toolTip().contains(QStringLiteral("12.0 Mbit/s")));

    // --- themes ---------------------------------------------------------------
    Card card(QStringLiteral("Total"));
    a.setTheme(ThemeId::Slate);
    const Theme slate = theme_palette(ThemeId::Slate);
    CHECK(dark_mode());
    CHECK(QApplication::palette().color(QPalette::Window) == slate.surface);
    CHECK(QApplication::palette().color(QPalette::Text) == slate.ink);
    CHECK(QApplication::style()->name().compare(QLatin1String("fusion"), Qt::CaseInsensitive) == 0);
    // Style-sheet ink is remade, not left in the previous theme's colour.
    QLabel* card_title = card.findChild<QLabel*>();
    CHECK(card_title && card_title->styleSheet().contains(slate.ink.name()));
    CHECK(usage.hostsNote()->styleSheet().contains(slate.ink_muted.name()));

    a.setTheme(ThemeId::Sand);
    CHECK(!dark_mode());
    CHECK(QApplication::palette().color(QPalette::Window) == theme_palette(ThemeId::Sand).surface);
    CHECK(card_title && card_title->styleSheet().contains(theme_palette(ThemeId::Sand).ink.name()));

    // Back to System: the desktop's own style and palette, as before any theme.
    a.setTheme(ThemeId::System);
    CHECK(QApplication::style()->name() == system_style);
    CHECK(QApplication::palette().color(QPalette::Window) == system_window);

    // --- the dialog -----------------------------------------------------------
    {
        QuotaInfo cur;
        cur.cap = 500'000'000'000;
        cur.cycle_day = 7;
        SettingsDialog d(cur, DaemonInfo{true, QStringLiteral("0.4.0"),
                                         QStringLiteral("/var/lib/bandsight/bandsight.db")});
        CHECK(tab_titles(d.sections()) ==
              (QStringList{QStringLiteral("General"), QStringLiteral("Display"),
                           QStringLiteral("About")}));
        CHECK(item_texts(d.rateCombo()) ==
              (QStringList{QStringLiteral("Mb/s - megabits per second (ISP speeds)"),
                           QStringLiteral("MB/s - megabytes per second (download speeds)")}));
        // Every theme is offered, System first.
        CHECK(d.themeCombo()->count() == all_themes().size());
        CHECK(d.themeCombo()->itemText(0) == QStringLiteral("System"));
        CHECK(!d.daemonSettingsChanged());

        // Picking applies at once ...
        d.rateCombo()->setCurrentIndex(1);
        CHECK(rate_unit() == RateUnit::Bytes);
        CHECK(tray.icon()->toolTip().contains(QStringLiteral("MB/s")));
        d.themeCombo()->setCurrentIndex(d.themeCombo()->findData(QStringLiteral("dark")));
        CHECK(current_theme() == ThemeId::Dark);
        CHECK(QApplication::palette().color(QPalette::Window) ==
              theme_palette(ThemeId::Dark).surface);
        // ... and Cancel puts it all back.
        d.reject();
        CHECK(rate_unit() == RateUnit::Bits);
        CHECK(current_theme() == ThemeId::System);
        CHECK(QApplication::palette().color(QPalette::Window) == system_window);
        CHECK(!QSettings().contains(QStringLiteral("appearance/theme")));
    }
    {
        SettingsDialog d(QuotaInfo{});
        d.volumeCombo()->setCurrentIndex(1);
        d.themeCombo()->setCurrentIndex(d.themeCombo()->findData(QStringLiteral("high-contrast")));
        CHECK(!d.daemonSettingsChanged());   // display only: nothing for the daemon
        d.applyLocal();                       // what OK does
        QSettings s;
        CHECK(s.value(QStringLiteral("appearance/theme")).toString() ==
              QStringLiteral("high-contrast"));
        CHECK(s.value(QStringLiteral("units/volume")).toInt() == int(VolumeUnit::Binary));
        CHECK(s.value(QStringLiteral("units/rate")).toInt() == int(RateUnit::Bits));
        d.daySpin()->setValue(9);
        CHECK(d.daemonSettingsChanged());
    }
    // A fresh start restores what was saved; an unknown key falls back.
    set_current_theme(ThemeId::System);
    set_volume_unit(VolumeUnit::Decimal);
    a.load();
    CHECK(current_theme() == ThemeId::HighContrast);
    CHECK(volume_unit() == VolumeUnit::Binary);
    QSettings().setValue(QStringLiteral("appearance/theme"), QStringLiteral("neon"));
    QSettings().setValue(QStringLiteral("units/rate"), 7);
    a.load();
    CHECK(current_theme() == ThemeId::System);
    CHECK(rate_unit() == RateUnit::Bits);
    CHECK(QApplication::palette().color(QPalette::Window) == system_window);
    QSettings().clear();
    a.load();

    // --- About ----------------------------------------------------------------
    {
        AboutPage about(DaemonInfo{true, QStringLiteral("0.4.0"), QStringLiteral("/srv/b.db")});
        CHECK(about.titleText() == QStringLiteral("Bandsight " BANDSIGHT_TEST_PROJECT_VERSION));
        CHECK(about.daemonText() == QStringLiteral("bandsightd 0.4.0"));
        CHECK(about.dbText() == QStringLiteral("/srv/b.db"));
        // A daemon whose hello predates the version field.
        about.setDaemonInfo(DaemonInfo{true, QString(), QStringLiteral("/srv/b.db")});
        CHECK(about.daemonText() == QStringLiteral("bandsightd: unknown"));
        about.setDaemonInfo(DaemonInfo{});
        CHECK(about.daemonText() == QStringLiteral("bandsightd: not connected"));

        about.setDaemonInfo(DaemonInfo{true, QStringLiteral("0.4.0"), QStringLiteral("/srv/b.db")});
        auto* copy = about.findChild<QPushButton*>(QStringLiteral("copyDetails"));
        CHECK(copy != nullptr);
        if (copy) copy->click();
        const QString details = QGuiApplication::clipboard()->text();
        const QString expect = build_details(DaemonInfo{true, QStringLiteral("0.4.0"),
                                                        QStringLiteral("/srv/b.db")});
        CHECK(details == expect);
        CHECK(expect.startsWith(QStringLiteral("Bandsight " BANDSIGHT_TEST_PROJECT_VERSION " (")));
        CHECK(expect.contains(QStringLiteral("\nbandsightd 0.4.0\n")));
        CHECK(expect.contains(QStringLiteral("\nQt ") + QString::fromLatin1(qVersion())));
        CHECK(expect.contains(QStringLiteral("\nKernel: Linux ")));
        CHECK(expect.contains(QStringLiteral("\nDatabase: /srv/b.db\n")));
    }

    // --- the entry points in the main window ----------------------------------
    {
        QTemporaryDir tmp;
        MainWindow w(tmp.filePath(QStringLiteral("sock")), tmp.filePath(QStringLiteral("db")));
        auto* tabs = w.findChild<QTabWidget*>();
        auto* gear = w.findChild<QToolButton*>(QStringLiteral("settingsButton"));
        CHECK(tabs && gear && tabs->cornerWidget(Qt::TopRightCorner) == gear);
        CHECK(gear && gear->toolTip() == QStringLiteral("Settings"));
        CHECK(gear && !gear->icon().isNull());
        auto* action = w.findChild<QAction*>(QStringLiteral("settingsAction"));
        CHECK(action && action->shortcut() == QKeySequence(QStringLiteral("Ctrl+,")));
        CHECK(w.actions().contains(action));

        CHECK(w.settingsDialog() == nullptr);
        if (gear) gear->click();
        SettingsDialog* first = w.settingsDialog();
        CHECK(first != nullptr && first->isVisible());
        // One dialog: the shortcut and the tray raise the open one.
        if (action) action->trigger();
        emit w.trayIcon()->settingsRequested();
        CHECK(w.settingsDialog() == first);
        CHECK(w.findChildren<SettingsDialog*>().size() == 1);

        // The daemon's hello reaches the open dialog's About section.
        HelloInfo h;
        h.db_path = tmp.filePath(QStringLiteral("db"));
        h.version = QStringLiteral("9.9.9");
        emit w.liveClient()->hello(h);
        CHECK(first && first->aboutPage()->daemonText() == QStringLiteral("bandsightd 9.9.9"));
        h.version.clear();
        emit w.liveClient()->hello(h);
        CHECK(first && first->aboutPage()->daemonText() == QStringLiteral("bandsightd: unknown"));
        CHECK(first && first->aboutPage()->dbText() == h.db_path);
        if (first) first->reject();
        QApplication::processEvents();   // WA_DeleteOnClose
        QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        CHECK(w.settingsDialog() == nullptr);
    }

    if (g_failures) std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    return g_failures ? 1 : 0;
}
