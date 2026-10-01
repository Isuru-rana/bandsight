// SPDX-License-Identifier: GPL-3.0-or-later
#include "live_client.h"
#include "frame_codec.h"

namespace bandsight::gui {

LiveClient::LiveClient(QString socket_path, QObject* parent)
    : QObject(parent), path_(std::move(socket_path)) {
    retry_.setSingleShot(true);
    connect(&retry_, &QTimer::timeout, this, &LiveClient::connectNow);
}

void LiveClient::start() { connectNow(); }

// INVARIANT: a QObject that emits signals to us must not be a value member.
// Make it a parented child, so ~QObject severs the connections before it dies.
//
// As a value member, `QLocalSocket sock_` was destroyed in reverse declaration
// order - i.e. AFTER retry_ and buf_. ~QLocalSocket closes a still-open
// connection and emits disconnected(), and that signal was still wired, because
// our ~QObject (which severs connections) had not run yet. onDisconnected() then
// ran on a half-destroyed object and called retry_.isActive()/retry_.start() on
// an already-destroyed QTimer: a use-after-free at teardown, which surfaced as
// "malloc(): unaligned tcache chunk" in ~46% of suite runs, at a random later
// allocation. As a child of `this`, sock_ instead dies inside
// QObject::deleteChildren(), which runs after our connections are torn down.
//
// Note this is NOT a Qt bug and NOT about reusing a socket across a failed
// connectToServer(): one raw QLocalSocket driven through the same
// connect-fail-retry-succeed sequence is clean over thousands of iterations.
// The hazard is ownership and destruction order, and it applies to any
// self-connected member QObject (Task 7's worker, Task 10/11's widgets).
//
// A fresh socket per attempt (rather than one long-lived child) also keeps the
// "unwire, then deleteLater" sequence honest: deleteLater() only defers
// destruction, so until it runs the corpse is still a live signal source.
void LiveClient::connectNow() {
    if (sock_) {
        sock_->disconnect(this);
        sock_->deleteLater();
    }
    sock_ = new QLocalSocket(this);
    connect(sock_, &QLocalSocket::connected, this, &LiveClient::onConnected);
    connect(sock_, &QLocalSocket::disconnected, this, &LiveClient::onDisconnected);
    connect(sock_, &QLocalSocket::errorOccurred, this,
            [this](QLocalSocket::LocalSocketError e) {
                last_error_ = e;
                onDisconnected();
            });
    connect(sock_, &QLocalSocket::readyRead, this, &LiveClient::onReadyRead);
    sock_->connectToServer(path_);
}

void LiveClient::onConnected() {
    retry_.stop();  // invariant: connected implies the retry loop is idle
    buf_.clear();
    // Clear the failure reason too: a caller reading lastError() while we are
    // Connected must not be handed the error from the attempt before this one.
    last_error_ = QLocalSocket::UnknownSocketError;
    state_ = State::Connected;
    emit stateChanged(state_);
}

void LiveClient::onDisconnected() {
    if (state_ != State::Disconnected) {
        state_ = State::Disconnected;
        emit stateChanged(state_);
    }
    // The timer hop is load-bearing, not stylistic: connectNow() failing emits
    // errorOccurred synchronously, which lands back here, so retrying inline
    // would recurse until the stack ran out.
    if (!retry_.isActive()) retry_.start(retry_ms_);
}

void LiveClient::onReadyRead() {
    buf_ += sock_->readAll();
    auto frames = extract_frames(buf_);
    if (!frames) {
        // Implausible length prefix: protocol corruption (spec §4.1). Drop the
        // connection rather than allocate or leave the parser stuck. abort()
        // does not emit disconnected(), so drive the state change by hand.
        sock_->abort();
        buf_.clear();  // the garbage must not survive into the next connection
        onDisconnected();
        return;
    }
    for (const QByteArray& body : *frames) {
        const QString t = frame_type(body);
        if (t == QLatin1String("hello")) {
            if (auto h = decode_hello(body)) emit hello(*h);
        } else if (t == QLatin1String("sample")) {
            if (auto s = decode_sample(body)) emit sample(*s);
        }
        else if (t == QLatin1String("ok")) {
            emit configResult(true, QString());
        } else if (t == QLatin1String("error")) {
            emit configResult(false, decode_error(body));
        }
        // pong: still ignored; nothing sends a ping.
    }
}

bool LiveClient::sendConfig(const QString& key, const QString& value) {
    if (state_ != State::Connected || !sock_) return false;
    // Newline-delimited, not length-prefixed: spec §6 makes the inbound
    // direction hand-typeable, and a command sent without the terminator hangs
    // forever with no error and no reply.
    const QByteArray line = QStringLiteral(
        "{\"cmd\":\"set_config\",\"key\":\"%1\",\"value\":\"%2\"}\n")
        .arg(key, value).toUtf8();
    return sock_->write(line) == line.size();
}

}  // namespace bandsight::gui
