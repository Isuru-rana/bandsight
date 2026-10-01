// SPDX-License-Identifier: GPL-3.0-or-later
#include "main_window.h"

#include <QCloseEvent>
#include <QSettings>
#include <QDateTime>
#include <QDebug>
#include <QLabel>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "failure.h"
#include "history_store.h"
#include "live_client.h"
#include "models/iface_model.h"
#include "tabs/graph_tab.h"
#include "tabs/iface_tab.h"
#include "quota.h"
#include "quota_bar.h"
#include "settings_dialog.h"
#include "units.h"
#include "tabs/usage_tab.h"
#include "widgets/timeline_strip.h"
#include "tray_icon.h"

namespace bandsight::gui {

namespace {

// Every cross-thread signal below carries one of these. Registered from the
// MainWindow constructor rather than from main(), so the test binary - which
// has its own main() - gets them too.
void register_metatypes() {
    static const bool once = [] {
        qRegisterMetaType<HelloInfo>();
        qRegisterMetaType<SampleFrame>();
        qRegisterMetaType<Window>();
        qRegisterMetaType<QVector<SeriesPoint>>();
        qRegisterMetaType<QVector<IfaceTotal>>();
        qRegisterMetaType<QVector<AppTotal>>();
        qRegisterMetaType<QuotaInfo>();
        qRegisterMetaType<QSet<int>>("QSet<int>");
        qRegisterMetaType<LiveClient::State>();
        return true;
    }();
    Q_UNUSED(once);
}

constexpr int kRefreshMs = 10'000;
constexpr int kBannerPollMs = 1'000;   // see the poll's own note in the ctor
// No hello means no db path from the daemon, but history may still be readable
// (spec §6.2) - so after this long, open the default path anyway.
constexpr int kOpenFallbackMs = 3'000;

}  // namespace

MainWindow::MainWindow(QString socket_path, QString db_path, QWidget* parent)
    : QMainWindow(parent),
      live_(new LiveClient(std::move(socket_path), this)),
      history_(new HistoryStore),   // no parent: the worker thread deletes it
      banner_(new QLabel),
      graph_(new GraphTab),
      ifaces_(new IfaceTab),
      usage_(new UsageTab),
      quota_(new QuotaBar),
      config_reply_timeout_(new QTimer(this)),
      refresh_(new QTimer(this)),
      tray_(new TrayIcon(this, this)),
      db_path_(std::move(db_path)),
      tray_available_(QSystemTrayIcon::isSystemTrayAvailable()) {
    register_metatypes();

    setWindowTitle(tr("bandsight"));
    resize(960, 600);

    banner_->setObjectName(QStringLiteral("banner"));   // findChild, for tests
    banner_->setWordWrap(true);
    // The usermod command in the wrong-group banner is only useful if it can be
    // copied out of it (spec §6.1).
    banner_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    banner_->hide();

    auto* tabs = new QTabWidget;
    tabs->addTab(graph_, tr("Graph"));
    // Spec §7 orders these Graph, Usage, Conn, Iface. ConnTab does not exist
    // yet, so Usage sits between the two that do rather than at the end.
    tabs->addTab(usage_, tr("Usage"));
    tabs->addTab(ifaces_, tr("Interfaces"));

    auto* central = new QWidget;
    auto* box = new QVBoxLayout(central);
    box->addWidget(banner_);
    box->addWidget(quota_);   // spec §7: above the tabs, always visible
    box->addWidget(tabs);
    setCentralWidget(central);

    // --- history worker -----------------------------------------------------
    // open(), every query and ~HistoryStore must all run on the thread that owns
    // the QSqlDatabase connection. moveToThread makes that this thread; the
    // finished->deleteLater pair makes the destructor run there too, which is
    // why history_ has no parent - a parent would delete it from here instead.
    history_->moveToThread(&history_thread_);
    connect(&history_thread_, &QThread::finished, history_, &QObject::deleteLater);
    history_thread_.start();

    // --- live path ----------------------------------------------------------
    connect(live_, &LiveClient::hello, this, &MainWindow::onHello);
    connect(live_, &LiveClient::sample, graph_, &GraphTab::onSample);
    connect(live_, &LiveClient::sample, tray_, &TrayIcon::onSample);
    connect(live_, &LiveClient::stateChanged, this, [this](LiveClient::State s) {
        if (s == LiveClient::State::Disconnected) graph_->onDisconnected();
        updateBanner();
    });

    // --- history path (all of these arrive from the worker thread) ----------
    connect(history_, &HistoryStore::opened, this, [this](const QString& path) {
        // A reply for a database we have since moved on from. Accepting it would
        // mark the CURRENT path open when nothing has opened it, after which
        // every query runs against the wrong file or none (B5).
        if (path != db_path_) return;
        db_open_ = true;
        history_failed_ = false;
        // Deliberately does NOT reset pending_series_. Two opens can be in
        // flight at once (two hellos naming different paths), and a reset
        // between the two opened() deliveries zeroes a count that a reply is
        // still owed against - after which the count goes negative and LATCHES
        // there, silently disabling the supersession guard below for the rest
        // of the session. Left alone, the same sequence self-corrects.
        updateBanner();
        refreshFromHistory();   // the queries held back until now
    });
    connect(history_, &HistoryStore::openFailed, this,
            [this](const QString& path, const QString& why) {
        if (path != db_path_) return;   // stale failure, same reasoning as opened()
        qWarning() << "bandsight: history unavailable:" << why;
        history_failed_ = true;   // db_open_ stays false: no query may be sent
        // A failed open is not a completed one. Without this, the canonical
        // first run dead-ends: no database yet, the 3 s fallback open fails,
        // the user follows the banner and starts the daemon, hello arrives
        // carrying the SAME path - so the reopen branch in onHello does not
        // fire - and startOpen() returns early forever. The banner would go
        // green while the table and every history window stayed empty for the
        // life of the process, which is worse than the banner it replaced.
        open_requested_ = false;
        updateBanner();
    });
    connect(history_, &HistoryStore::series, this,
            [this](Window w, QVector<SeriesPoint> pts) {
                // Constraint: GraphTab's own guard matches on Window alone, and
                // a visibility toggle re-asks for the SAME window, so a reply to
                // a superseded request would pass it and briefly redraw the
                // pre-toggle interface set. The worker answers in FIFO order, so
                // "still outstanding after this one" is exactly "superseded".
                //
                // COUPLING: this counter is only correct because
                // HistoryStore::querySeries emits series() on EVERY path,
                // including a failed query (history_store.cpp:107, outside the
                // if/else). Exactly one reply per request is what keeps the
                // count balanced; a defensive early return added there would
                // ratchet it up permanently, after which every single-request
                // cycle would have its only reply dropped and history would
                // stop updating silently. Nothing resets this count - see
                // opened() for why a reset makes that failure mode worse
                // rather than better.
                if (--pending_series_ > 0) return;
                graph_->onSeries(w, std::move(pts));
            });
    connect(history_, &HistoryStore::ifaceTotals, this,
            [this](const QVector<IfaceTotal>& totals) {
                ifaces_->model()->setWindow(totals_window_);
                ifaces_->setTotals(totals);
                if (sel_to_ > sel_from_)
                    usage_->setPeriodRange(sel_from_, sel_to_);
                else
                    usage_->setPeriod(window_label(totals_window_));
                // The headline split comes from the INTERFACE totals, not from
                // summing the applications: bytes the collector could not
                // attribute to a process are still real traffic, and must not
                // vanish from the total just because nobody owned them.
                quint64 rx = 0, tx = 0;
                for (const IfaceTotal& t : totals)
                    if (kind_is_measured(t.kind)) { rx += t.rx; tx += t.tx; }
                usage_->setSplit(rx, tx);
                // The graph card's bars cover the whole window, like the chart
                // above them; a Usage timeline selection narrows these totals,
                // so a ranged answer is not theirs.
                if (!(sel_to_ > sel_from_)) graph_->setIfaceTotals(totals);
                // setTotals deliberately emits nothing (Task 8), so newly
                // appeared interfaces reach the graph only from here.
                applyVisible(ifaces_->model()->visibleIfindexes());
            });

    connect(history_, &HistoryStore::quota, quota_, &QuotaBar::setQuota);
    connect(history_, &HistoryStore::quota, tray_, &TrayIcon::onQuota);
    connect(history_, &HistoryStore::appTotals, this,
            [this](const QVector<AppTotal>& totals) {
                usage_->setTotals(totals);
            });

    // --- widgets ------------------------------------------------------------
    connect(tray_, &TrayIcon::settingsRequested, this, &MainWindow::openSettings);
    config_reply_timeout_->setObjectName(QStringLiteral("configReplyTimeout"));  // findChild, for tests
    config_reply_timeout_->setSingleShot(true);
    config_reply_timeout_->setInterval(3000);
    connect(config_reply_timeout_, &QTimer::timeout, this, [this] {
        if (settings_)
            settings_->showResult(
                false, tr("bandsightd did not answer. It may be an older version "
                          "that cannot store settings - restart it after "
                          "installing this build."));
    });

    connect(live_, &LiveClient::configResult, this,
            [this](bool ok, const QString& msg) {
                config_reply_timeout_->stop();
                if (settings_) settings_->showResult(ok, msg);
                // A stored cap changes what the bar means, so re-read rather
                // than waiting up to ten seconds for the next refresh.
                if (ok) refreshFromHistory();
            });

    // Units are this user's preference, not the machine's: they live in
    // QSettings, and must be restored before the first frame is formatted.
    {
        QSettings s;
        set_rate_unit(RateUnit(s.value(QStringLiteral("units/rate"),
                                       int(RateUnit::Bits)).toInt()));
        set_volume_unit(VolumeUnit(s.value(QStringLiteral("units/volume"),
                                           int(VolumeUnit::Decimal)).toInt()));
    }

    connect(graph_, &GraphTab::seriesNeeded, this, &MainWindow::requestSeries);

    // The graph's combo is the single source of truth for the window; the Usage
    // screen asks it to change and is told the answer, so the two can never
    // disagree about the period every number on screen is reported over.
    connect(usage_, &UsageTab::windowChangeRequested, graph_, &GraphTab::setWindow);
    connect(graph_, &GraphTab::windowChanged, this, [this](Window w) {
        usage_->setWindow(w);
        // A span chosen on the 5-minute view is still inside the 30-day one, so
        // it would survive the switch on range alone and silently filter the new
        // window down to five minutes. Changing the period discards it.
        usage_->timeline()->clearSelection();
        // An exclusion chosen for one period means nothing for another, and a
        // filter the user cannot see is a filter they will misread.
        usage_->clearExcluded();
        usage_->setPeriod(window_label(w));
        refreshFromHistory();   // totals are per-window; do not wait for the timer
    });
    // The Usage timeline mirrors exactly what the graph plotted, whatever the
    // source. On the 5-minute window that is the live ring and no history query
    // is ever issued, so a strip fed from series() alone stayed empty there.
    connect(graph_, &GraphTab::plotted, usage_, &UsageTab::setSeries);

    connect(usage_->timeline(), &TimelineStrip::rangeSelected, this,
            [this](qint64 from, qint64 to) {
                sel_from_ = from;
                sel_to_ = to;
                refreshFromHistory();
            });
    connect(usage_->timeline(), &TimelineStrip::selectionCleared, this, [this] {
        sel_from_ = sel_to_ = 0;
        refreshFromHistory();
    });
    connect(ifaces_->model(), &IfaceModel::visibleChanged,
            this, &MainWindow::applyVisible);

    refresh_->setObjectName(QStringLiteral("historyRefresh"));  // findChild, for tests
    connect(refresh_, &QTimer::timeout, this, &MainWindow::refreshFromHistory);
    refresh_->start(kRefreshMs);   // the no-query-before-opened gate is inside

    // LiveClient signals transitions, and a socket that fails the same way every
    // 2 s never transitions - so the wrong-group user's EACCES, which is the
    // most likely first-run state and the banner that outranks all others, would
    // otherwise never reach the screen: the window would sit there saying
    // "bandsightd is not running" while the daemon runs fine. Hence a poll.
    // It costs two stat() calls, and QLabel::setText ignores an unchanged string.
    auto* banner_poll = new QTimer(this);
    connect(banner_poll, &QTimer::timeout, this, &MainWindow::updateBanner);
    banner_poll->start(kBannerPollMs);

    QTimer::singleShot(kOpenFallbackMs, this, [this] { startOpen(); });

    updateBanner();   // a dead socket never emits stateChanged: it starts down
    live_->start();
}

MainWindow::~MainWindow() {
    // Quit and wait, in that order: finished() then fires deleteLater on the
    // worker, and QThread processes deferred deletes as it winds down, so
    // ~HistoryStore closes its connection on the thread that opened it. wait()
    // guarantees that has happened before this object stops existing.
    history_thread_.quit();
    history_thread_.wait();
}

// Closing hides to the tray; Quit in the tray menu is the only way out
// (spec §4.7). Neither ends monitoring - bandsightd collects regardless.
void MainWindow::closeEvent(QCloseEvent* e) {
    if (!tray_available_) {
        // No tray to click (GNOME without an extension, spec §8.3). Hiding here
        // would leave a running process with no window and no icon: the app
        // would be unquittable by any normal means. Accepting the close closes
        // the last window, and QApplication::quitOnLastWindowClosed - true by
        // default, and nothing here changes it - ends the process.
        QMainWindow::closeEvent(e);
        return;
    }
    hide();
    e->ignore();
}

QString MainWindow::bannerText() const {
    return banner_->isHidden() ? QString() : banner_->text();
}

void MainWindow::updateBanner() {
    Probe p = probe_filesystem(db_path_);
    p.socket_ok = live_->state() == LiveClient::State::Connected;
    // lastError() only means anything while the socket is down, and classify()
    // ignores socket_error when socket_ok is true - so reading it in the other
    // branch could only ever smuggle a stale SocketAccessError into the one
    // banner that outranks all the others (spec §6.1).
    if (!p.socket_ok) p.socket_error = live_->lastError();

    const AppState s = classify(p);   // priority lives there, never here
    QString text = banner_text(s, db_path_);
    // Two states, and only two, may carry the degraded-history suffix (I3).
    //
    // Empty banner (Ok): a database that exists but will not open - corrupt,
    // mid-WAL-recovery, on a full disk - passes every probe classify() makes,
    // so the state is Ok and banner_text() is empty. Gating this on DaemonDown
    // left the graph's history windows and the whole Interfaces tab silently
    // empty with nothing on screen to say why; the suffix is the only thing
    // that can speak for that state.
    //
    // DaemonDown: "restarts" is literally the next thing that happens, so the
    // sentence reads as a continuation of "start it with systemctl".
    //
    // NotInGroup and NoDatabase are deliberately excluded. Spec §6.1 shows one
    // banner at a time, and both of these are made self-contradictory by the
    // suffix: a non-traversable state directory guarantees the open fails too,
    // so NotInGroup would routinely end with "until bandsightd restarts" -
    // advice that sends the user in the circle §6.1 exists to prevent, when the
    // fix is a group change and a re-login. NoDatabase would claim the daemon
    // "is running" and needs to restart in one breath.
    if (history_failed_ && (text.isEmpty() || s == AppState::DaemonDown)) {
        if (!text.isEmpty()) text += QLatin1Char(' ');
        text += QStringLiteral("History unavailable until bandsightd restarts.");
    }

    banner_->setText(text);
    banner_->setVisible(!text.isEmpty());
}

void MainWindow::onHello(const HelloInfo& h) {
    if (!h.db_path.isEmpty() && h.db_path != db_path_) {
        db_path_ = h.db_path;     // the daemon knows better than the default
        open_requested_ = false;  // whatever we may have opened was the wrong file
        db_open_ = false;
    }
    // LiveRing sums only what is visible, so an empty set draws a flat live graph
    // however much the daemon sends. Seed it from hello - but only while the
    // table is empty, or a reconnect would re-tick boxes the user unticked.
    if (ifaces_->model()->rowCount() == 0) {
        QSet<int> v;
        for (const HelloIfaceInfo& i : h.ifaces)
            if (kind_is_measured(i.kind)) v.insert(i.id);
        applyVisible(v);
    }
    startOpen();
    updateBanner();   // db_path_ appears in the no-database banner
}

void MainWindow::startOpen() {
    if (open_requested_) return;
    open_requested_ = true;
    // Queued onto the worker: a QSqlDatabase must be opened on the thread that
    // will use and close it, and this call is on the GUI thread.
    QMetaObject::invokeMethod(history_,
                              [h = history_, p = db_path_] { h->open(p); });
}

void MainWindow::refreshFromHistory() {
    if (!db_open_) return;   // see requestSeries
    const Window w = graph_->window();
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    totals_window_ = w;
    const bool ranged = sel_to_ > sel_from_;
    const int res = window_spec(w).res;
    const qint64 from = sel_from_, to = sel_to_;
    QMetaObject::invokeMethod(history_, [h = history_, w, now, ranged, res, from, to] {
        if (ranged) {
            // Same signals, same tier: a selection narrows what is counted, not
            // how it is counted, so nothing downstream needs to know one exists.
            h->queryIfaceTotalsRange(from, to, res);
            h->queryAppTotalsRange(from, to, res);
        } else {
            h->queryIfaceTotals(w, now);
            h->queryAppTotals(w, now);
        }
        // Independent of the graph's window and of any selection: a cap runs on
        // its own cycle and is not something the timeline can narrow.
        h->queryQuota(now);
    });
    // History windows refresh every time. A live window asks only while it is
    // still waiting for its seed: once seeded the ring carries it, and a query
    // every ten seconds would fight the live frames for the same seconds.
    if (!is_live(w) || graph_->needsSeed()) requestSeries(w, visible_, now);
}

void MainWindow::requestSeries(Window w, QSet<int> ifindexes, qint64 now) {
    // A query issued before opened() does not fail loudly: querySeries answers
    // with a full all-zero series that reads exactly like an idle network, and
    // queryIfaceTotals answers with an empty table. Neither is distinguishable
    // from real data, so the only defence is never to ask (Task 7's header).
    // Nothing is lost by dropping the request: opened() re-issues it.
    if (!db_open_) return;
    ++pending_series_;
    QMetaObject::invokeMethod(history_, [h = history_, w, ifindexes, now] {
        h->querySeries(w, ifindexes, now);
    });
}

// The cap and the cycle day go to the DAEMON, because it owns the database and
// the GUI's handle is read-only (spec §3). Units and autostart are this user's
// and are applied here. Nothing is written optimistically: the bar updates when
// the daemon confirms and the database has actually changed.
void MainWindow::openSettings() {
    if (settings_) { settings_->raise(); settings_->activateWindow(); return; }

    auto* d = new SettingsDialog(quota_->info(), this);
    d->setAttribute(Qt::WA_DeleteOnClose);
    settings_ = d;
    connect(d, &QDialog::accepted, this, [this, d] {
        d->applyLocal();
        // Refuse silently-lost writes: with the daemon down there is nothing to
        // store the cap, and pretending otherwise would show a cap the next
        // start would not remember.
        if (!live_->sendConfig(QStringLiteral("cap_bytes"),
                               QString::number(d->capBytes())) ||
            !live_->sendConfig(QStringLiteral("cycle_start_day"),
                               QString::number(d->cycleDay()))) {
            d->showResult(false, tr("bandsightd is not running, so the cap was "
                                    "not saved. Units were applied."));
            return;
        }
        // The confirmation arrives on configResult - or does not, if the daemon
        // is too old to know the command, which is what the timeout is for.
        config_reply_timeout_->start();
        // The graph and tables repaint immediately for the unit change either way.
        graph_->update();
        update();
    });
    d->show();
}

void MainWindow::applyVisible(QSet<int> v) {
    if (v == visible_) return;   // a refresh that changed nothing asks nothing
    visible_ = std::move(v);
    graph_->onVisibleChanged(visible_);
}

}  // namespace bandsight::gui
