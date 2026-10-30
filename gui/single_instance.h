// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QByteArray>
#include <QLockFile>
#include <QObject>
#include <QString>

class QLocalServer;

namespace bandsight::gui {

// One GUI per user. Closing hides to the tray (spec §4.7), so every extra
// launcher click used to leave another window and another tray icon running.
// A later launch now asks the running one to show itself, then exits.
//
// The lock file decides who is first, not the socket: a connect that fails says
// nothing about whether another launch is mid-startup, and two launches racing
// on removeServer() would each unlink the other's socket. QLockFile also clears
// the lock of a process that died without releasing it.
class SingleInstance : public QObject {
    Q_OBJECT
public:
    // dir: where the lock and socket live - the user's runtime dir in the app,
    // so the instance is per user and per session.
    explicit SingleInstance(const QString& dir, QObject* parent = nullptr);

    // true: this process is the instance and should start normally.
    // false: another one is running and has been sent `token`; exit.
    bool claim(const QByteArray& token);

signals:
    // A later launch wants the window. `token` is its XDG_ACTIVATION_TOKEN:
    // without one, KWin refuses to raise a window the user did not just click.
    void activationRequested(const QByteArray& token);

private:
    void onConnection();

    QString sock_path_;
    QLockFile lock_;
    QLocalServer* server_ = nullptr;
};

}  // namespace bandsight::gui
