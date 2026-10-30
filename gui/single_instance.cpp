// SPDX-License-Identifier: GPL-3.0-or-later
#include "single_instance.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QThread>

namespace bandsight::gui {

SingleInstance::SingleInstance(const QString& dir, QObject* parent)
    : QObject(parent),
      sock_path_(dir + QStringLiteral("/bandsight-gui.sock")),
      lock_(dir + QStringLiteral("/bandsight-gui.lock")) {
    lock_.setStaleLockTime(0);   // stale only when its pid is dead, never by age
}

bool SingleInstance::claim(const QByteArray& token) {
    if (lock_.tryLock(0)) {
        // A crashed instance leaves its socket file behind, and listen() will
        // not reuse it. Holding the lock is what makes removing it safe.
        QLocalServer::removeServer(sock_path_);
        server_ = new QLocalServer(this);
        server_->setSocketOptions(QLocalServer::UserAccessOption);
        connect(server_, &QLocalServer::newConnection, this, &SingleInstance::onConnection);
        // A failed listen still leaves us the instance: later launches cannot
        // reach us and will exit, which beats opening a second window.
        if (!server_->listen(sock_path_))
            qWarning("bandsight: cannot listen on %s: %s", qPrintable(sock_path_),
                     qPrintable(server_->errorString()));
        return true;
    }

    // The holder may still be starting up, between taking the lock and
    // listening, so give it a moment rather than failing on the first try.
    QLocalSocket s;
    for (int i = 0; i < 20; ++i) {
        s.connectToServer(sock_path_);
        if (s.waitForConnected(100)) break;
        QThread::msleep(100);
    }
    if (s.state() == QLocalSocket::ConnectedState) {
        s.write(token + '\n');
        s.waitForBytesWritten(1000);
        s.disconnectFromServer();
    } else {
        qWarning("bandsight: already running, but it did not answer");
    }
    return false;
}

void SingleInstance::onConnection() {
    while (QLocalSocket* c = server_->nextPendingConnection()) {
        connect(c, &QLocalSocket::disconnected, c, &QObject::deleteLater);
        auto take = [this, c] {
            if (!c->canReadLine()) return;
            emit activationRequested(c->readLine().trimmed());
            c->disconnectFromServer();
        };
        connect(c, &QLocalSocket::readyRead, this, take);
        take();   // the line may have arrived before readyRead was connected
    }
}

}  // namespace bandsight::gui
