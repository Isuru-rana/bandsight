// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>
#include <QWidget>

class QLabel;

namespace bandsight::gui {

// What the GUI knows about the daemon, from its hello frame.
struct DaemonInfo {
    bool connected = false;
    QString version;   // empty: the daemon's hello has no version field
    QString db_path;
};

// This build, from the generated build_info.h and git_commit.h.
QString app_version();      // PROJECT_VERSION, e.g. "0.4.0"
QString about_title();      // "Bandsight 0.4.0"
QString git_commit();       // short hash, empty outside a git checkout
QString build_type();       // CMAKE_BUILD_TYPE, or "unspecified"
QString kernel_text();      // uname: "Linux 6.8.0-45-generic x86_64"
// "bandsightd 0.4.0", "bandsightd: unknown" for a daemon whose hello has no
// version, "bandsightd: not connected" with no daemon.
QString daemon_line(const DaemonInfo& d);
// The block the Copy details button puts on the clipboard, for bug reports.
QString build_details(const DaemonInfo& d);

// The About section of the settings dialog.
class AboutPage : public QWidget {
    Q_OBJECT
public:
    explicit AboutPage(const DaemonInfo& d, QWidget* parent = nullptr);
    void setDaemonInfo(const DaemonInfo& d);

    // Test-only.
    QString titleText() const;
    QString daemonText() const;
    QString dbText() const;

private:
    void applyInk();

    DaemonInfo daemon_;
    QLabel* title_;
    QLabel* daemon_label_;
    QLabel* db_label_;
    QLabel* link_;
    QList<QLabel*> muted_;
};

}  // namespace bandsight::gui
