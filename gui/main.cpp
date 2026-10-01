// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QIcon>
#include <QMessageBox>
#include <QSqlDatabase>

#include "main_window.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("bandsight");
    // Wayland takes the window icon from the desktop entry matching the app id,
    // never from setWindowIcon; X11 and the taskbar fallback use the icon below.
    app.setDesktopFileName("bandsight");
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("bandsight"),
                                       QIcon(QStringLiteral(":/icons/bandsight.png"))));

    // libqt6sql6-sqlite is a separate package and a classic missing-dependency
    // failure (spec §9). Fail loudly, not with an empty window.
    if (!QSqlDatabase::isDriverAvailable("QSQLITE")) {
        QMessageBox::critical(nullptr, "bandsight",
            "Qt SQLite driver missing. Install libqt6sql6-sqlite.");
        return 1;
    }

    // The metatypes every cross-thread signal needs are registered by the
    // MainWindow constructor, so the test binary's own main() gets them too.
    bandsight::gui::MainWindow w;
    w.show();
    return app.exec();
}
