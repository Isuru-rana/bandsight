// SPDX-License-Identifier: GPL-3.0-or-later
#include "failure.h"

#include <QDir>
#include <QFileInfo>

namespace bandsight::gui {

AppState classify(const Probe& p) {
    // Group membership explains every other symptom - diagnose it first
    // (spec §6.1: this banner wins over all others).
    const bool sock_denied =
        !p.socket_ok && p.socket_error == QLocalSocket::SocketAccessError;
    const bool dir_denied = p.db_dir_exists && !p.db_dir_traversable;
    if (sock_denied || dir_denied) return AppState::NotInGroup;

    // Daemon not running (socket failed) - this is true regardless of db state.
    // The daemon itself creates the database when it first runs, so if the socket
    // is down, the actionable fix is to start the daemon. Only check for missing
    // database if the daemon is actually running (socket OK).
    if (!p.socket_ok) {
        return AppState::DaemonDown;
    }

    // Socket is OK, so daemon is running. Check if it has created the database yet.
    if (p.db_dir_traversable && !p.db_file_exists) return AppState::NoDatabase;
    return AppState::Ok;
}

QString banner_text(AppState s, const QString& db_path) {
    switch (s) {
        case AppState::Ok:
            return {};
        case AppState::NotInGroup: {
            QString user = qEnvironmentVariable("USER");
            if (user.isEmpty()) user = qEnvironmentVariable("LOGNAME");
            if (user.isEmpty()) user = QStringLiteral("<you>");
            return QStringLiteral(
                "Cannot access bandsightd (permission denied). Run "
                "`sudo usermod -aG bandsight %1`, then log out and back in.").arg(user);
        }
        case AppState::DaemonDown:
            return QStringLiteral(
                "bandsightd is not running. Start it with `sudo systemctl start bandsightd`.");
        case AppState::NoDatabase:
            return QStringLiteral(
                "bandsightd is running but there is no database at `%1`. "
                "Check `sudo journalctl -u bandsightd` for why it could not create one.").arg(db_path);
    }
    return {};
}

Probe probe_filesystem(const QString& db_path) {
    Probe p;
    const QFileInfo dir(QFileInfo(db_path).absolutePath());
    p.db_dir_exists = dir.exists();
    p.db_dir_traversable = dir.isExecutable();
    p.db_file_exists = p.db_dir_traversable && QFileInfo::exists(db_path);
    return p;
}

}  // namespace bandsight::gui
