// SPDX-License-Identifier: GPL-3.0-or-later
// `bandsight-cli top`: the loop. Reads keys from the terminal, live samples
// from the daemon's socket (never writing to it) and the history every few
// seconds, and redraws. Everything it shows is laid out by top_view.cpp.
//
// Strings are built by appending pieces, never "literal" + std::string or
// insert() (a false GCC 12 -Wrestrict, fatal under -Werror on Debian 12).
#include <poll.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <memory>

#include "args.h"
#include "commands.h"
#include "core/history_db.h"
#include "daemon_socket.h"
#include "terminal.h"
#include "top_input.h"
#include "top_view.h"

namespace bandsight::cli {

namespace {

using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;

constexpr auto kHistoryEvery = std::chrono::seconds(5);
constexpr auto kReconnectEvery = std::chrono::seconds(2);
// No sample for this long: the rate shown would be a stale number.
constexpr auto kRateStale = std::chrono::seconds(3);
// A lone ESC is the Escape key once nothing follows it for this long.
constexpr int kEscapeMs = 40;

volatile std::sig_atomic_t g_resized = 0;
void on_winch(int) { g_resized = 1; }
void on_hangup(int) { request_stop(); }

void err(const std::string& s) {
    std::fputs(s.c_str(), stderr);
    std::fputc('\n', stderr);
}

// Unicode blocks unless the locale says the terminal cannot show them.
bool utf8_locale() {
    const char* v = nullptr;
    for (const char* name : {"LC_ALL", "LC_CTYPE", "LANG"})
        if ((v = std::getenv(name)) && *v) break;
    if (!v || !*v) return false;
    std::string s;
    for (const char* p = v; *p; ++p) s += char(std::tolower(static_cast<unsigned char>(*p)));
    return s.find("utf-8") != std::string::npos || s.find("utf8") != std::string::npos;
}

// Installs a handler for as long as it lives, and puts the old one back.
class ScopedSignal {
public:
    ScopedSignal(int sig, void (*fn)(int)) : sig_(sig) {
        struct sigaction sa {};
        sa.sa_handler = fn;
        sigemptyset(&sa.sa_mask);
        // No SA_RESTART: poll() must return so the loop sees the flag.
        ::sigaction(sig, &sa, &old_);
    }
    ~ScopedSignal() { ::sigaction(sig_, &old_, nullptr); }
    ScopedSignal(const ScopedSignal&) = delete;
    ScopedSignal& operator=(const ScopedSignal&) = delete;

private:
    int sig_;
    struct sigaction old_ {};
};

// std::terminate (an exception nobody catches, a noexcept violation) has no
// scope to unwind, so the terminal is restored from here before aborting.
class ScopedTerminate {
public:
    ScopedTerminate() : old_(std::set_terminate(handler)) {}
    ~ScopedTerminate() { std::set_terminate(old_); }
    ScopedTerminate(const ScopedTerminate&) = delete;
    ScopedTerminate& operator=(const ScopedTerminate&) = delete;

private:
    [[noreturn]] static void handler() {
        Terminal::restore_active();
        std::abort();
    }
    std::terminate_handler old_;
};

class Top {
public:
    Top(const Options& o, const Env& env, Terminal& term)
        : o_(o), env_(env), term_(term) {
        const std::size_t p = top_period_index(o.period);
        state_.period = p == std::string::npos ? top_period_index("24h") : p;
        state_.rate = o.bytes ? core::RateUnit::Bytes : core::RateUnit::Bits;
        state_.volume = o.binary ? core::VolumeUnit::Binary : core::VolumeUnit::Decimal;
        look_.colour = env.tty && !env.no_color;
        look_.unicode = !o.ascii && utf8_locale();
    }

    int run() {
        resize();
        connect();
        reload();
        for (;;) {
            if (stop_requested()) return kExitOk;
            if (g_resized) {
                g_resized = 0;
                resize();
            }
            const auto now = Clock::now();
            if (!daemon_ && now - last_connect_ >= kReconnectEvery) {
                const std::string before = db_path();
                connect();
                // The daemon may name a different database than the default.
                if (db_path() != before) reload();
            }
            if (now >= next_history_) reload();
            if (live_.have_rate && now - last_sample_ > kRateStale) {
                live_.have_rate = false;
                dirty_ = true;
            }
            if (std::time(nullptr) != drawn_at_) dirty_ = true;   // the clock
            if (dirty_) draw();

            if (!wait()) return kExitOk;
            if (quit_) return kExitOk;
        }
    }

private:
    std::int64_t clock() const { return env_.clock_fixed ? env_.now : std::int64_t(std::time(nullptr)); }

    std::string db_path() const {
        if (!o_.db_path.empty()) return o_.db_path;
        if (daemon_ && !daemon_->hello().db_path.empty()) return daemon_->hello().db_path;
        return kDefaultDb;
    }

    void resize() {
        if (!term_.size(cols_, rows_)) {
            cols_ = 80;
            rows_ = 24;
        }
        full_ = dirty_ = true;
    }

    void connect() {
        last_connect_ = Clock::now();
        auto d = std::make_unique<DaemonSocket>();
        live_ = LiveView{};
        live_.state = d->connect(o_.socket_path, 1000);
        live_.error = d->error();
        if (live_.state == DaemonSocket::State::Running) {
            live_.version = d->hello().version;
            daemon_ = std::move(d);
        }
        dirty_ = true;
    }

    void lost(const std::string& why) {
        daemon_.reset();
        live_ = LiveView{};
        live_.state = DaemonSocket::State::NotRunning;
        live_.error = why;
        last_connect_ = Clock::now();
        dirty_ = true;
    }

    void reload() {
        next_history_ = Clock::now() + kHistoryEvery;
        dirty_ = true;
        const std::string path = db_path();
        if (!db_.is_open() || path != opened_) {
            opened_ = path;
            if (!db_.open(path)) {
                const std::string why = db_error(path, db_);
                db_.close();
                data_.history = HistoryView{};
                data_.history.error = "Cannot read the database.";
                for (Panel& p : data_.history.panels) p.empty_text = "No history to show.";
                data_.history.panels[std::size_t(PanelId::Apps)].empty_text = why;
                return;
            }
        }
        data_.history = load_history(db_, state_.period, clock());
    }

    void draw() {
        const Layout l = layout_for(cols_, rows_, state_.focus);
        clamp_scroll(state_, data_, l);
        page_ = std::size_t(std::max(1, l.visible[state_.focus]));
        data_.now = clock();
        data_.live = live_;
        const auto lines = render_top(state_, data_, l, look_);
        std::string out;
        if (full_) out += "\033[2J";
        for (std::size_t i = 0; i < lines.size(); ++i) {
            out += "\033[";
            out += std::to_string(i + 1);
            out += ";1H";
            out += lines[i];
        }
        term_.write(out);
        drawn_at_ = std::time(nullptr);
        dirty_ = full_ = false;
    }

    // Waits for a key, a sample, or the next thing due. false when the
    // terminal has gone away.
    bool wait() {
        int timeout = 1000;
        const auto until_history =
            std::chrono::duration_cast<milliseconds>(next_history_ - Clock::now()).count();
        timeout = int(std::clamp<long long>(until_history, 0, timeout));
        if (keys_.pending()) timeout = std::min(timeout, kEscapeMs);

        pollfd fds[2] = {{STDIN_FILENO, POLLIN, 0}, {daemon_ ? daemon_->fd() : -1, POLLIN, 0}};
        const int r = ::poll(fds, 2, timeout);
        if (r < 0) return true;   // a signal: the loop looks at the flags
        if (r == 0) {
            if (keys_.pending()) handle(keys_.flush());
            return true;
        }
        if (fds[1].revents) read_daemon();
        if (fds[0].revents & POLLIN) {
            char buf[256];
            const ssize_t n = ::read(STDIN_FILENO, buf, sizeof buf);
            if (n == 0) return false;   // end of input: the terminal hung up
            if (n < 0 && errno != EINTR && errno != EAGAIN) return false;
            if (n > 0) handle(keys_.feed(std::string_view(buf, std::size_t(n))));
        } else if (fds[0].revents & (POLLHUP | POLLERR | POLLNVAL)) {
            return false;
        }
        return true;
    }

    void read_daemon() {
        for (;;) {
            core::Sample s;
            switch (daemon_->next_sample(0, s)) {
            case DaemonSocket::Next::Sample:
                add_sample(live_, s);
                last_sample_ = Clock::now();
                dirty_ = true;
                continue;
            case DaemonSocket::Next::Timeout:
            case DaemonSocket::Next::Interrupted:
                return;
            case DaemonSocket::Next::Closed:
                lost("the daemon closed the connection");
                return;
            case DaemonSocket::Next::Corrupt:
                lost(daemon_->error());
                return;
            }
        }
    }

    void handle(const std::vector<Key>& keys) {
        for (const Key& k : keys) {
            switch (apply_key(state_, k, page_)) {
            case Action::None: break;
            case Action::Redraw: dirty_ = true; break;
            case Action::Reload: reload(); break;
            case Action::Quit: quit_ = true; return;
            }
        }
    }

    const Options& o_;
    const Env& env_;
    Terminal& term_;
    TopState state_;
    Look look_;
    TopData data_;
    LiveView live_;
    KeyDecoder keys_;
    std::unique_ptr<DaemonSocket> daemon_;
    core::HistoryDb db_;
    std::string opened_;
    int cols_ = 80, rows_ = 24;
    std::size_t page_ = 1;
    bool dirty_ = true, full_ = true, quit_ = false;
    std::time_t drawn_at_ = 0;
    Clock::time_point last_connect_{}, last_sample_{}, next_history_{};
};

}  // namespace

int cmd_top(const Options& o, const Env& env) {
    if (!env.tty || ::isatty(STDIN_FILENO) != 1) {
        err("bandsight-cli top needs a terminal. In a script, use 'bandsight-cli status' or "
            "'bandsight-cli live' (both take --json).");
        return kExitUsage;
    }
    Terminal term(STDIN_FILENO, STDOUT_FILENO);
    const ScopedSignal winch(SIGWINCH, on_winch);
    const ScopedSignal hup(SIGHUP, on_hangup);
    const ScopedTerminate terminate;
    std::string why;
    if (!term.enter(&why)) {
        std::string msg = "Cannot use the terminal: ";
        msg += why;
        err(msg);
        return kExitRuntime;
    }
    try {
        Top top(o, env, term);
        const int code = top.run();
        term.restore();
        return code;
    } catch (const std::exception& e) {
        term.restore();
        std::string msg = "bandsight-cli top: ";
        msg += e.what();
        err(msg);
        return kExitRuntime;
    }
}

}  // namespace bandsight::cli
