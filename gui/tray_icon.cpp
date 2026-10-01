// SPDX-License-Identifier: GPL-3.0-or-later
#include "tray_icon.h"

#include <QApplication>
#include <QCoreApplication>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QMenu>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QWidget>

#include "units.h"

namespace bandsight::gui {

TrayIcon::TrayIcon(QWidget* window, QObject* parent)
    : QObject(parent), window_(window), icon_(new QSystemTrayIcon(this)) {
    // The app's own icon, from hicolor once installed and from the bundled qrc
    // copy when run from the build tree. The theme chain stays as a last
    // resort: a QSystemTrayIcon with a null icon is an item the user cannot
    // click, and B12 showed Breeze has no network-transmit-receive at all.
    applyIcon();
    icon_->setToolTip(tr("bandsight"));

    // Parented to the window: setContextMenu does not take ownership.
    auto* menu = new QMenu(window_);
    QAction* toggle = menu->addAction(tr("Show/Hide"));
    connect(toggle, &QAction::triggered, this, &TrayIcon::toggleWindow);
    // "Quit" quits this viewer. Collection is the daemon's job and continues.
    QAction* settings = menu->addAction(tr("Settings…"));
    connect(settings, &QAction::triggered, this, &TrayIcon::settingsRequested);
    menu->addSeparator();
    QAction* quit = menu->addAction(tr("Quit"));
    connect(quit, &QAction::triggered, qApp, &QCoreApplication::quit);
    icon_->setContextMenu(menu);

    connect(icon_, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason r) {
                if (r == QSystemTrayIcon::Trigger) toggleWindow();
            });

    icon_->show();
}

// The base icon, tinted by cap state. Recomposed rather than swapped for a
// second named theme icon: no theme ships an "over quota network" icon, and B12
// is the standing lesson that naming an icon no installed theme has renders
// nothing at all.
void TrayIcon::applyIcon() {
    QIcon base = QIcon::fromTheme(QStringLiteral("bandsight"),
                                  QIcon(QStringLiteral(":/icons/bandsight.png")));
    if (base.availableSizes().isEmpty() && base.name().isEmpty())
        base = QIcon::fromTheme(
            QStringLiteral("network-transmit-receive"),
            QIcon::fromTheme(QStringLiteral("network-wired"),
                             QApplication::style()->standardIcon(QStyle::SP_DriveNetIcon)));
    if (cap_percent_ < 80) {
        icon_->setIcon(base);
        return;
    }

    const QSize size(22, 22);
    QPixmap pm = base.pixmap(size);
    if (pm.isNull()) { icon_->setIcon(base); return; }

    QPainter p(&pm);
    p.setCompositionMode(QPainter::CompositionMode_SourceAtop);
    p.fillRect(pm.rect(), cap_percent_ >= 100 ? QColor(192, 57, 43, 170)
                                              : QColor(214, 137, 16, 170));
    p.end();
    icon_->setIcon(QIcon(pm));
}

void TrayIcon::onQuota(const QuotaInfo& info) {
    const int pct = info.cap == 0 ? -1 : int((info.used * 100) / info.cap);
    if (pct == cap_percent_) return;   // repainting an unchanged icon flickers
    cap_percent_ = pct;
    applyIcon();
}

void TrayIcon::onSample(const SampleFrame& f) {
    // Summed fresh from THIS frame every time. Nothing is carried over from the
    // previous one, which is what makes an interface missing from a frame read
    // as zero rather than as its last known rate (spec §4.3).
    quint64 rx = 0, tx = 0;
    for (const SampleIface& i : f.ifaces) {
        rx += i.rx;
        tx += i.tx;
    }
    // Says what it counts (FOLLOWUP B6). The tooltip sums every measured
    // interface; the graph sums only the ones the user has left ticked. Both are
    // right - a tray indicator is about the machine, and the chart is a filtered
    // view - but with an interface unchecked they sit one above the other
    // disagreeing, and nothing said why. Confirmed live: the graph flatlined at
    // zero while this kept reading 7.7 kbit/s down.
    //
    // Labelled rather than filtered: the tray outlives the window, and an
    // indicator that goes quiet because of a checkbox in a hidden window is a
    // worse failure than one that reads high.
    icon_->setToolTip(tr("↓ %1  ↑ %2\nAll interfaces (the Graph tab shows only "
                         "the ones you have ticked)")
                          .arg(format_rate(rx), format_rate(tx)));
}

void TrayIcon::toggleWindow() {
    if (window_->isVisible()) {
        window_->hide();
    } else {
        window_->showNormal();   // a hide-to-tray may have left it minimised
        window_->raise();
        window_->activateWindow();
    }
}

}  // namespace bandsight::gui
