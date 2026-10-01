// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QMainWindow>
#include <QSet>
#include <QString>
#include <QPointer>
#include <QThread>

#include "frame_types.h"

class QCloseEvent;
class QLabel;
class QTimer;

namespace bandsight::gui {

class GraphTab;
class HistoryStore;
class IfaceTab;
class QuotaBar;
class SettingsDialog;
class UsageTab;
class LiveClient;
class TrayIcon;

// Glue, and nothing else: every decision it makes lives in a component it is
// connecting (spec §4.4). The two paths it joins are deliberately asymmetric -
// live frames arrive on this thread, history answers arrive from a worker one.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    // Both paths are constructor arguments purely so tests can point them at a
    // fixture socket and a fixture database. Nothing reads them from anywhere
    // else and there is no settings file.
    explicit MainWindow(
        QString socket_path = QStringLiteral("/run/bandsightd/sock"),
        QString db_path = QStringLiteral("/var/lib/bandsight/bandsight.db"),
        QWidget* parent = nullptr);
    ~MainWindow() override;

    // Test-only, same rationale as GraphTab's line accessors: the wiring IS this
    // class, and none of it is observable from outside otherwise.
    QString bannerText() const;   // empty when the banner is hidden
    GraphTab* graphTab() const { return graph_; }
    IfaceTab* ifaceTab() const { return ifaces_; }
    UsageTab* usageTab() const { return usage_; }
    QuotaBar* quotaBar() const { return quota_; }
    HistoryStore* historyStore() const { return history_; }
    LiveClient* liveClient() const { return live_; }
    TrayIcon* trayIcon() const { return tray_; }

    // Test-only, same rationale as LiveClient::setRetryInterval: the close
    // behaviour branches on whether a system tray exists, and under the
    // offscreen platform QSystemTrayIcon::isSystemTrayAvailable() reports
    // whatever that platform happens to report - which is not the desktop the
    // branch is about. Injecting it is the only way to test both sides.
    void setTrayAvailable(bool v) { tray_available_ = v; }

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void updateBanner();
    void onHello(const HelloInfo& h);
    void startOpen();                 // idempotent; the only caller of open()
    void refreshFromHistory();        // totals, plus series when not on the ring
    void requestSeries(Window w, QSet<int> ifindexes, qint64 now);
    void applyVisible(QSet<int> v);
    void openSettings();

    LiveClient* live_;
    QThread history_thread_;
    HistoryStore* history_;           // lives on history_thread_, unparented
    QLabel* banner_;
    GraphTab* graph_;
    IfaceTab* ifaces_;
    UsageTab* usage_;
    QuotaBar* quota_;
    // Held only while the modeless dialog is open, so the daemon's answer can
    // be shown next to the value that caused it.
    QPointer<SettingsDialog> settings_;
    // A daemon that predates set_config ignores the command entirely: the write
    // succeeds, no reply is ever sent, and without this the dialog waits
    // forever showing nothing. Spec §6 documents the same hazard for a command
    // sent without its newline.
    QTimer* config_reply_timeout_;
    QTimer* refresh_;
    TrayIcon* tray_;
    QString db_path_;
    QSet<int> visible_;
    // The window the outstanding totals queries were issued for. Shared by the
    // interface and application tables: refreshFromHistory dispatches both in
    // the same call, so one field covers both replies.
    // The window the outstanding totals query was issued for. The reply does not
    // carry it, and the header must name the period the numbers actually cover -
    // not whichever window the combo happens to be on when they land.
    Window totals_window_ = Window::FiveMin;
    // A sub-range chosen on the timeline. {0,0} means the whole window, which is
    // the only state the rest of the code knows about - a selection changes what
    // the totals cover, never how they are asked for.
    qint64 sel_from_ = 0;
    qint64 sel_to_ = 0;
    bool db_open_ = false;            // gates every query (see requestSeries)
    bool open_requested_ = false;
    bool history_failed_ = false;
    int pending_series_ = 0;
    bool tray_available_;             // see setTrayAvailable; read by closeEvent
};

}  // namespace bandsight::gui
