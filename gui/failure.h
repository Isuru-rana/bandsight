// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QLocalSocket>
#include <QString>

namespace bandsight::gui {

enum class AppState { Ok, NotInGroup, DaemonDown, NoDatabase };

struct Probe {
    bool socket_ok = false;
    QLocalSocket::LocalSocketError socket_error = QLocalSocket::UnknownSocketError;
    bool db_dir_exists = false;
    bool db_dir_traversable = false;   // QFileInfo(dir).isExecutable()
    bool db_file_exists = false;       // only meaningful if traversable
};

AppState classify(const Probe&);
QString banner_text(AppState, const QString& db_path);  // empty for Ok
Probe probe_filesystem(const QString& db_path);          // fills the db_* fields

}  // namespace bandsight::gui
