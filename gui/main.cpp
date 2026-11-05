// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QIcon>
#include <QMessageBox>
#include <QSqlDatabase>
#include <QStandardPaths>

#include <cstdio>
#include <cstring>

#include "build_info.h"
#include "main_window.h"
#include "single_instance.h"

int main(int argc, char* argv[]) {
    // Before QApplication: answering --version must not need a display, and
    // must not hand over to (or become) the running instance.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::printf("bandsight %s\n", BANDSIGHT_VERSION);
            return 0;
        }
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            std::printf("Usage: bandsight [--version] [--help] [Qt options]\n"
                        "Network usage viewer for bandsightd.\n");
            return 0;
        }
    }

    QApplication app(argc, argv);
    app.setApplicationName("bandsight");
    // Wayland takes the window icon from the desktop entry matching the app id,
    // never from setWindowIcon; X11 and the taskbar fallback use the icon below.
    app.setDesktopFileName("bandsight");
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("bandsight"),
                                       QIcon(QStringLiteral(":/icons/bandsight.png"))));

    // Before anything that could show a window: a second launch must only hand
    // over to the first. The token is the launcher's, and the running instance
    // needs it to be allowed to raise itself on Wayland.
    bandsight::gui::SingleInstance instance(
        QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation));
    if (!instance.claim(qgetenv("XDG_ACTIVATION_TOKEN"))) return 0;

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
    QObject::connect(&instance, &bandsight::gui::SingleInstance::activationRequested,
                     &w, [&w](const QByteArray& token) {
        // Qt's xdg-shell integration reads this when the window is activated.
        if (!token.isEmpty()) qputenv("XDG_ACTIVATION_TOKEN", token);
        w.showNormal();   // a hide-to-tray may have left it minimised
        w.raise();
        w.activateWindow();
    });
    return app.exec();
}
