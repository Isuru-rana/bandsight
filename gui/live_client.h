// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QLocalSocket>
#include <QObject>
#include <QTimer>
#include "frame_types.h"

namespace bandsight::gui {

// Wraps QLocalSocket: owns frame reassembly and reconnection (spec §4.1).
// Reads are a stream of length-prefixed frames; the one write path is
// sendConfig, which is newline-delimited - the framing is deliberately
// asymmetric (spec §6). No queue and no threading (QLocalSocket is already
// async on the main thread).
class LiveClient : public QObject {
    Q_OBJECT
public:
    enum class State { Disconnected, Connected };

    explicit LiveClient(QString socket_path, QObject* parent = nullptr);

    // Belt and braces for the teardown hazard documented at connectNow(): stop
    // the socket signalling us before any of our members start dying.
    ~LiveClient() override {
        if (sock_) sock_->disconnect(this);
    }

    void start();  // begins connecting; retries every retry_ms_ forever
    State state() const { return state_; }
    QLocalSocket::LocalSocketError lastError() const { return last_error_; }
    void setRetryInterval(int ms) { retry_ms_ = ms; }

    // Asks the daemon to store a config value (spec §7). Returns false without
    // sending when the socket is down, so a caller never mistakes "not
    // delivered" for "sent and awaiting a reply". The answer arrives later on
    // configResult - the daemon validates, not us.
    bool sendConfig(const QString& key, const QString& value);

signals:
    void hello(const bandsight::gui::HelloInfo&);
    void sample(const bandsight::gui::SampleFrame&);
    void stateChanged(bandsight::gui::LiveClient::State);

    // ok == false carries the daemon's own reason, which is the only place the
    // allowlist and range rules are stated - the GUI must not duplicate them
    // and then disagree.
    void configResult(bool ok, const QString& message);

private:
    void onReadyRead();
    void onConnected();
    void onDisconnected();  // also drives retry scheduling
    void connectNow();      // always on a freshly constructed socket

    QString path_;
    QLocalSocket* sock_ = nullptr;
    QTimer retry_;
    QByteArray buf_;
    State state_ = State::Disconnected;
    QLocalSocket::LocalSocketError last_error_ = QLocalSocket::UnknownSocketError;
    int retry_ms_ = 2000;  // spec §6: retries every 2 seconds
};

}  // namespace bandsight::gui

Q_DECLARE_METATYPE(bandsight::gui::LiveClient::State)
