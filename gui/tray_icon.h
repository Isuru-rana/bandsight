// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>

#include "frame_types.h"
#include "quota.h"

class QSystemTrayIcon;
class QWidget;

namespace bandsight::gui {

// The tray presence (spec §4.7): a themed icon, a tooltip carrying the current
// rates, and a two-item menu. It is a VIEWER and nothing else - bandsightd
// collects from boot whether or not this application is running, so no text
// here may suggest that quitting stops monitoring.
//
// Ownership: the QSystemTrayIcon is parented to this object, and the QMenu to
// the window (a QMenu needs a QWidget parent, and setContextMenu does not take
// ownership - an unparented menu is the classic leak here).
class TrayIcon : public QObject {
    Q_OBJECT
public:
    explicit TrayIcon(QWidget* window, QObject* parent = nullptr);

    // Test-only: the tooltip and the menu are the whole observable surface.
    QSystemTrayIcon* icon() const { return icon_; }

signals:
    // The tray owns the menu, but not what Settings means - MainWindow owns the
    // socket the dialog needs and shows it.
    void settingsRequested();

public:
    // Test-only, same rationale: the tint has no other observable surface.
    int capPercent() const { return cap_percent_; }

public slots:
    void onSample(const bandsight::gui::SampleFrame& f);

    // Spec §7 tints the icon amber above 80% of the data cap and red above
    // 100%. Until QuotaBar existed nothing ever called this, so the rule was
    // written down and never ran.
    void onQuota(const bandsight::gui::QuotaInfo& info);

private:
    void toggleWindow();

    void applyIcon();

    QWidget* window_;
    QSystemTrayIcon* icon_;
    int cap_percent_ = -1;   // -1 = no cap configured
};

}  // namespace bandsight::gui
