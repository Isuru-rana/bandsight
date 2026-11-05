// SPDX-License-Identifier: GPL-3.0-or-later
// bandsight-cli end to end: the real binary against a fixture database (and a
// fake daemon socket), with NO_COLOR and TZ=UTC so the text is reproducible.
//
//   - every command's text output against tests/cli_golden.txt (set
//     BANDSIGHT_UPDATE_GOLDEN=1 to rewrite it, then review the diff);
//   - every command's --json parsed and its keys and numbers checked;
//   - exit codes and the plain-words errors: missing, unreadable and foreign
//     databases, usage mistakes;
//   - `live` and `status` against a fake daemon that sends recorded frames and
//     checks the client never writes a byte;
//   - argument, period and date parsing, directly.
//
// argv: <bandsight-cli> <golden file>
#include <fcntl.h>
#include <grp.h>
#include <poll.h>
#include <pty.h>
#include <termios.h>
#include <sqlite3.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <csignal>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#include "args.h"
#include "terminal.h"
#include "check.h"
#include "fixture_db.h"
#include "json_frame.h"
#include "json_value.h"

using namespace bandsight;
using bandsight::fixture::kFixtureNow;

namespace {

std::string g_cli;
std::string g_tmp;

struct Result {
    std::string out, err;
    int code = -1;
};

std::string slurp(int fd) {
    std::string s;
    char buf[4096];
    ssize_t n;
    while ((n = ::read(fd, buf, sizeof buf)) > 0) s.append(buf, std::size_t(n));
    return s;
}

// Runs the CLI with args and the reproducible environment. `as_nobody` drops
// to an unprivileged user first (root reads every file, so a permission error
// can only be produced without it).
Result run(const std::vector<std::string>& args, bool as_nobody = false,
           const std::vector<std::string>& extra_env = {}) {
    int out[2], err[2];
    CHECK(::pipe(out) == 0 && ::pipe(err) == 0);
    const pid_t pid = ::fork();
    if (pid == 0) {
        ::dup2(out[1], 1);
        ::dup2(err[1], 2);
        ::close(out[0]);
        ::close(err[0]);
        if (as_nobody && ::geteuid() == 0 &&
            (::setgroups(0, nullptr) != 0 || ::setgid(65534) != 0 || ::setuid(65534) != 0))
            ::_exit(99);
        std::vector<std::string> env{"TZ=UTC", "NO_COLOR=1", "LC_ALL=C",
                                     "BANDSIGHT_CLI_NOW=" + std::to_string(kFixtureNow)};
        for (const auto& e : extra_env) env.push_back(e);
        std::vector<char*> envp, argv;
        for (auto& e : env) envp.push_back(e.data());
        envp.push_back(nullptr);
        // The unprivileged user may not be able to reach the build tree (a
        // private home or scratch directory), so it runs a copy placed in the
        // test's own world-readable directory.
        const std::string exe = as_nobody ? g_tmp + "/bandsight-cli" : g_cli;
        std::vector<std::string> a{exe};
        a.insert(a.end(), args.begin(), args.end());
        for (auto& s : a) argv.push_back(s.data());
        argv.push_back(nullptr);
        ::execve(exe.c_str(), argv.data(), envp.data());
        ::_exit(98);
    }
    ::close(out[1]);
    ::close(err[1]);
    Result r;
    // Both pipes drained together, so a chatty stderr cannot block the child.
    std::thread t([&] { r.err = slurp(err[0]); });
    r.out = slurp(out[0]);
    t.join();
    ::close(out[0]);
    ::close(err[0]);
    int status = 0;
    ::waitpid(pid, &status, 0);
    r.code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return r;
}

std::string replace_all(std::string s, const std::string& from, const std::string& to) {
    for (std::size_t at = 0; (at = s.find(from, at)) != std::string::npos; at += to.size())
        s.replace(at, from.size(), to);
    return s;
}

// --- golden text -------------------------------------------------------------

std::string g_golden;   // what this run produced, in file form

void golden(const std::string& title, const std::vector<std::string>& args, int expect_code,
            bool as_nobody = false) {
    const Result r = run(args, as_nobody);
    CHECK(r.code == expect_code);
    if (r.code != expect_code)
        std::fprintf(stderr, "%s: exit %d, expected %d\nstderr: %s\n", title.c_str(), r.code,
                     expect_code, r.err.c_str());
    std::string cmd = "bandsight-cli";
    for (const auto& a : args) cmd += " " + (a.find(' ') != std::string::npos ? "\"" + a + "\"" : a);
    g_golden += "### " + title + "\n$ " + replace_all(cmd, g_tmp, "$TMP") + "\n" +
                replace_all(r.out, g_tmp, "$TMP");
    if (!r.err.empty()) g_golden += "[stderr]\n" + replace_all(r.err, g_tmp, "$TMP");
    g_golden += "[exit " + std::to_string(r.code) + "]\n\n";
}

// --- fake daemon -------------------------------------------------------------

// Listens on a Unix socket like bandsightd: greets each client with a hello,
// sends the scripted sample frames, then holds the connection until the client
// leaves. Counts every byte a client sends - there must be none.
class FakeDaemon {
public:
    FakeDaemon(std::string path, std::string hello, std::vector<std::string> frames,
               bool close_after = false)
        : path_(std::move(path)), hello_(std::move(hello)), frames_(std::move(frames)),
          close_after_(close_after) {
        ::unlink(path_.c_str());
        fd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        sockaddr_un a{};
        a.sun_family = AF_UNIX;
        std::strncpy(a.sun_path, path_.c_str(), sizeof a.sun_path - 1);
        CHECK(::bind(fd_, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0);
        CHECK(::listen(fd_, 4) == 0);
        thread_ = std::thread([this] { serve(); });
    }
    ~FakeDaemon() {
        stop_ = true;
        thread_.join();
        ::close(fd_);
        ::unlink(path_.c_str());
    }
    std::size_t bytes_received() const { return received_; }
    int clients() const { return clients_; }

private:
    void serve() {
        while (!stop_) {
            pollfd p{fd_, POLLIN, 0};
            if (::poll(&p, 1, 50) <= 0) continue;
            const int c = ::accept4(fd_, nullptr, nullptr, SOCK_CLOEXEC);
            if (c < 0) continue;
            ++clients_;
            send_all(c, frame(hello_));
            for (const auto& f : frames_) {
                send_all(c, frame(f));
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            if (close_after_) {
                drain(c, 200);
            } else {
                // Until the client hangs up (or the test ends).
                while (!stop_ && drain(c, 50)) {}
            }
            ::close(c);
        }
    }
    // false once the peer has closed.
    bool drain(int c, int ms) {
        pollfd p{c, POLLIN, 0};
        if (::poll(&p, 1, ms) <= 0) return true;
        char buf[256];
        const ssize_t n = ::recv(c, buf, sizeof buf, MSG_DONTWAIT);
        if (n > 0) received_ += std::size_t(n);
        return n > 0;
    }
    static void send_all(int c, const std::string& s) {
        std::size_t at = 0;
        while (at < s.size()) {
            const ssize_t n = ::send(c, s.data() + at, s.size() - at, MSG_NOSIGNAL);
            if (n <= 0) return;
            at += std::size_t(n);
        }
    }

    std::string path_, hello_;
    std::vector<std::string> frames_;
    bool close_after_;
    int fd_ = -1;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<std::size_t> received_{0};
    std::atomic<int> clients_{0};
};

std::vector<std::string> recorded_samples(int n) {
    // Ten seconds of a download on eth0 with a little Wi-Fi, one frame a second
    // as the daemon sends them; an idle interface is left out of its frame.
    std::vector<std::string> f;
    for (int i = 0; i < n; ++i) {
        std::vector<FrameIface> ifs{{2, std::uint64_t(1'250'000 + 125'000 * i), 31'250}};
        if (i % 2 == 0) ifs.push_back({3, 2'000, 500});
        f.push_back(encode_sample(kFixtureNow - n + i + 1, ifs, {}));
    }
    return f;
}

// --- JSON --------------------------------------------------------------------

std::optional<JsonValue> json_of(const Result& r) {
    CHECK(r.code == 0);
    CHECK(!r.out.empty() && r.out.back() == '\n' && r.out.find('\n') == r.out.size() - 1);
    auto v = parse_json(r.out);
    CHECK(v.has_value());
    if (!v) std::fprintf(stderr, "not JSON: %s\n", r.out.c_str());
    return v;
}

double num(const JsonValue* v) { return v && v->type == JsonValue::Type::Number ? v->num : -1; }
std::string str(const JsonValue* v) { return v && v->type == JsonValue::Type::String ? v->str : "<none>"; }
bool is_null(const JsonValue* v) { return v && v->type == JsonValue::Type::Null; }

void check_bytes(const JsonValue& o) {
    CHECK(num(o.get("down_bytes")) >= 0 && num(o.get("up_bytes")) >= 0);
    CHECK(num(o.get("total_bytes")) == num(o.get("down_bytes")) + num(o.get("up_bytes")));
}

void json_checks(const std::string& db) {
    {
        const auto v = json_of(run({"apps", "--json", "--db", db, "--limit", "3"}));
        if (v) {
            CHECK(str(v->get("command")) == "apps");
            const JsonValue* p = v->get("period");
            CHECK(p && str(p->get("name")) == "24h" && num(p->get("resolution_s")) == 60);
            CHECK(p && str(p->get("from")) == "2026-11-04T12:00:30+00:00");
            CHECK(p && str(p->get("to")) == "2026-11-05T12:00:30+00:00");
            const JsonValue* apps = v->get("apps");
            CHECK(apps && apps->arr.size() == 3);
            if (apps && !apps->arr.empty()) {
                const JsonValue& a = apps->arr[0];
                CHECK(str(a.get("label")) == "Firefox" && str(a.get("exe")) == "/usr/lib/firefox/firefox");
                // Raw bytes: 5 h + 3 min + 40 s ago, unformatted.
                CHECK(num(a.get("down_bytes")) == 656'500'000.0);
                check_bytes(a);
            }
            const JsonValue* o = v->get("others");
            CHECK(o && num(o->get("count")) == 5);
            const JsonValue* u = v->get("unattributed");
            CHECK(u && num(u->get("down_bytes")) == 421'000'000.0);
            if (u) check_bytes(*u);
            const JsonValue* t = v->get("total");
            CHECK(t && num(t->get("down_bytes")) == 1'424'429'000.0);
            // Every row, the fold and Unattributed add up to the total.
            double sum = 0;
            for (const auto& a : apps->arr) sum += num(a.get("down_bytes"));
            sum += num(o->get("down_bytes")) + num(u->get("down_bytes"));
            CHECK(sum == num(t->get("down_bytes")));
        }
    }
    {
        // --limit 0: everything, no fold.
        const auto v = json_of(run({"apps", "--json", "--db", db, "--limit", "0"}));
        CHECK(v && v->get("apps")->arr.size() == 8 && is_null(v->get("others")));
    }
    {
        const auto v = json_of(run({"hosts", "--json", "--db", db, "--limit", "0"}));
        if (v) {
            CHECK(v->get("collected")->b && is_null(v->get("recorded_since")) && is_null(v->get("app")));
            bool lan = false, none = false;
            for (const auto& h : v->get("hosts")->arr) {
                check_bytes(h);
                if (str(h.get("ip")) == "192.168.1.10")
                    lan = str(h.get("zone")) == "lan" && is_null(h.get("hostname"));
                if (is_null(h.get("ip"))) none = str(h.get("zone")) == "none" &&
                                                 str(h.get("label")) == "No peer address";
                if (str(h.get("hostname")) == "registry.npmjs.org") {
                    // Both node binaries talk to it; the larger first.
                    const auto& apps = h.get("apps")->arr;
                    CHECK(apps.size() == 2 && str(apps[0].get("label")) == "node" &&
                          str(apps[1].get("label")) == "node" &&
                          str(apps[0].get("exe")) != str(apps[1].get("exe")) &&
                          num(apps[0].get("total_bytes")) >= num(apps[1].get("total_bytes")));
                }
            }
            CHECK(lan && none);
        }
    }
    {
        const auto v = json_of(run({"hosts", "--json", "--db", db, "--app", "Spotify", "--period", "7d"}));
        CHECK(v && str(v->get("app")) == "Spotify" && v->get("hosts")->arr.size() == 1 &&
              is_null(v->get("unattributed")) && is_null(v->get("total")));
    }
    {
        const auto v = json_of(run({"types", "--json", "--db", db, "--period", "30d"}));
        if (v) {
            CHECK(str(v->get("recorded_since")) == "2026-10-16T12:00:30+00:00");
            bool https = false;
            for (const auto& t : v->get("types")->arr) {
                check_bytes(t);
                if (str(t.get("service")) == "HTTPS") {
                    const auto& ports = t.get("ports")->arr;
                    https = ports.size() == 1 && str(ports[0].get("proto")) == "tcp" &&
                            num(ports[0].get("port")) == 443;
                }
                if (str(t.get("service")) == "Other") CHECK(t.get("ports")->arr.empty());
            }
            CHECK(https);
        }
    }
    {
        const auto v = json_of(run({"ifaces", "--json", "--all", "--db", db}));
        if (v) {
            CHECK(v->get("interfaces")->arr.size() == 6);
            for (const auto& i : v->get("interfaces")->arr) {
                check_bytes(i);
                if (str(i.get("name")) == "eth0")
                    CHECK(i.get("measured")->b && i.get("ids")->arr.size() == 1 &&
                          num(&i.get("ids")->arr[0]) == 2);
                if (str(i.get("name")) == "lo") CHECK(!i.get("measured")->b);
            }
        }
        const auto measured = json_of(run({"ifaces", "--json", "--db", db}));
        CHECK(measured && measured->get("interfaces")->arr.size() == 2);
    }
    {
        // The cycle as a period, and an explicit range.
        const auto c = json_of(run({"apps", "--json", "--db", db, "--period", "cycle"}));
        CHECK(c && str(c->get("period")->get("name")) == "cycle" &&
              str(c->get("period")->get("from")) == "2026-11-01T00:00:00+00:00");
        const auto r = json_of(run({"apps", "--json", "--db", db, "--from", "2026-11-05T11:30",
                                    "--to", "2026-11-05T12:00"}));
        CHECK(r && str(r->get("period")->get("name")) == "custom" &&
              num(r->get("period")->get("resolution_s")) == 1);
    }
    {
        const auto s = json_of(run({"status", "--json", "--db", db, "--socket", g_tmp + "/none"}));
        if (s) {
            const JsonValue* d = s->get("daemon");
            CHECK(d && !d->get("running")->b && str(d->get("state")) == "not_running" &&
                  is_null(d->get("version")));
            const JsonValue* c = s->get("cycle");
            CHECK(c && num(c->get("cap_bytes")) == 1e11 && num(c->get("renewal_day")) == 1 &&
                  str(c->get("start")) == "2026-11-01T00:00:00+00:00" &&
                  str(c->get("end")) == "2026-12-01T00:00:00+00:00");
            CHECK(c && num(c->get("used_bytes")) > 0 && num(c->get("projected_bytes")) > num(c->get("used_bytes")));
            CHECK(is_null(s->get("live")));
        }
    }
}

// --- daemon-facing commands ----------------------------------------------------

// Starts `live`, waits for its first line, then sends Ctrl-C (SIGINT).
Result interrupt_live(const std::string& sock) {
    int out[2];
    CHECK(::pipe(out) == 0);
    const pid_t pid = ::fork();
    if (pid == 0) {
        ::dup2(out[1], 1);
        ::close(out[0]);
        const int null = ::open("/dev/null", O_WRONLY);
        ::dup2(null, 2);
        const std::string now = "BANDSIGHT_CLI_NOW=" + std::to_string(kFixtureNow);
        char* envp[] = {const_cast<char*>("TZ=UTC"), const_cast<char*>(now.c_str()), nullptr};
        char* argv[] = {g_cli.data(), const_cast<char*>("live"), const_cast<char*>("--socket"),
                        const_cast<char*>(sock.c_str()), nullptr};
        ::execve(g_cli.c_str(), argv, envp);
        ::_exit(98);
    }
    ::close(out[1]);
    Result r;
    char c;
    while (::read(out[0], &c, 1) == 1) {
        r.out += c;
        if (c == '\n') break;
    }
    ::kill(pid, SIGINT);
    r.out += slurp(out[0]);
    ::close(out[0]);
    int status = 0;
    ::waitpid(pid, &status, 0);
    r.code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return r;
}

void daemon_checks(const std::string& db) {
    const std::string sock = g_tmp + "/sock";
    const std::string hello = encode_hello(db, {{2, "eth0", "physical"}, {3, "wlan0", "wifi"}}, "0.5.0");
    {
        FakeDaemon d(sock, hello, recorded_samples(10));
        golden("status, daemon running, rates in bytes", {"status", "--socket", sock, "--bytes"}, 0);
        // The database the daemon names is the one read, without --db.
        golden("apps, database named by the daemon", {"apps", "--socket", sock, "--limit", "2"}, 0);
        golden("live, three lines", {"live", "--socket", sock, "--count", "3"}, 0);
        golden("live, two-second average", {"live", "--socket", sock, "--interval", "2", "--count", "2"}, 0);

        const Result j = run({"live", "--socket", sock, "--json", "--count", "2"});
        CHECK(j.code == 0 && j.err.empty());
        std::istringstream lines(j.out);
        std::string line;
        int n = 0;
        while (std::getline(lines, line)) {
            const auto v = parse_json(line);
            CHECK(v.has_value());
            if (!v) continue;
            ++n;
            CHECK(num(v->get("interval_s")) == 1);
            CHECK(num(v->get("down_bytes_per_s")) >= 1'250'000);
            const auto& ifs = v->get("interfaces")->arr;
            CHECK(!ifs.empty() && str(ifs[0].get("name")) == "eth0");
            CHECK(str(v->get("ts")).size() == 25);
        }
        CHECK(n == 2);

        const auto st = json_of(run({"status", "--json", "--socket", sock}));
        if (st) {
            CHECK(st->get("daemon")->get("running")->b && str(st->get("daemon")->get("version")) == "0.5.0");
            CHECK(str(st->get("database")->get("path")) == db);
            CHECK(num(st->get("live")->get("down_bytes_per_s")) == 1'252'000);
        }
        // Ctrl-C ends it cleanly, exit 0, whatever the daemon is doing.
        const Result i = interrupt_live(sock);
        CHECK(i.code == 0 && i.out.find("down") != std::string::npos);

        // Read only: across every connection above, not one byte came back.
        CHECK(d.clients() == 7);
        CHECK(d.bytes_received() == 0);
    }
    {
        // An old daemon: no version in its hello.
        FakeDaemon d(sock, encode_hello(db, {}), recorded_samples(1));
        const Result r = run({"status", "--socket", sock});
        CHECK(r.code == 0 && r.out.find("running (version not reported)") != std::string::npos);
    }
    {
        // The daemon goes away mid-stream.
        FakeDaemon d(sock, hello, recorded_samples(2), true);
        golden("live, daemon stops", {"live", "--socket", sock}, 1);
    }
    {
        // Something listening that never greets.
        FakeDaemon d(sock, "{\"t\":\"pong\"}", {});
        const Result r = run({"live", "--socket", sock});
        CHECK(r.code == 1 && r.err.find("did not answer") != std::string::npos);
    }
    golden("live, daemon not running", {"live", "--socket", g_tmp + "/none"}, 1);
}

// --- parsing, directly -----------------------------------------------------------

void parsing() {
    std::string why;
    const auto parse = [&](std::vector<std::string> a) {
        a.insert(a.begin(), "bandsight-cli");
        return cli::parse_args(a, &why);
    };
    CHECK(parse({"apps"}) && parse({"apps"})->period == "24h" && parse({"apps"})->limit == 10);
    for (const char* p : {"5m", "30m", "1h", "6h", "12h", "24h", "7d", "30d", "cycle"})
        CHECK(parse({"apps", "--period", p}).has_value());
    CHECK(parse({"apps", "--period=7d"}) && parse({"apps", "--period=7d"})->period == "7d");
    CHECK(!parse({"apps", "--period", "2w"}) && why.find("Unknown period") != std::string::npos);
    CHECK(!parse({"apps", "--period", "1h", "--from", "2026-11-01"}));
    CHECK(!parse({"apps", "--to", "2026-11-01"}));
    CHECK(!parse({"apps", "--from", "2026-11-05", "--to", "2026-11-04"}));
    CHECK(!parse({"apps", "--from", "yesterday"}));
    CHECK(!parse({"apps", "--limit", "-1"}) && !parse({"apps", "--limit", "x"}));
    CHECK(parse({"apps", "--limit", "0"})->limit == 0);
    CHECK(!parse({"types", "--limit", "3"}) && why.find("does not take") != std::string::npos);
    CHECK(!parse({"apps", "--app", "x"}));
    CHECK(!parse({"apps", "--all"}));
    CHECK(!parse({"--period", "1h", "apps"}) && why.find("goes after a command") != std::string::npos);
    CHECK(!parse({"live", "--interval", "0"}) && !parse({"live", "--interval", "1.5"}));
    CHECK(parse({"live", "--interval", "5"})->interval == 5);
    CHECK(!parse({"frobnicate"}) && why.find("Unknown command") != std::string::npos);
    CHECK(!parse({}) && why.find("No command") != std::string::npos);
    CHECK(!parse({"apps", "extra"}));
    CHECK(!parse({"apps", "--limit"}) && why.find("needs a value") != std::string::npos);
    CHECK(parse({"--json", "--db", "/x", "apps", "--bytes", "--binary"}));
    {
        const auto o = parse({"--json", "--db", "/x", "apps", "--bytes", "--binary"});
        CHECK(o->json && o->bytes && o->binary && o->db_path == "/x");
    }
    CHECK(parse({"--help"})->help && parse({"hosts", "--help"})->command == cli::Command::Hosts);
    CHECK(parse({"--version"})->version && parse({"-V"})->version);

    // Dates: local time (TZ=UTC here), an explicit zone, and nonsense refused.
    CHECK(cli::parse_datetime("2026-11-05") == 1'793'836'800);
    CHECK(cli::parse_datetime("2026-11-05T12:00") == 1'793'880'000);
    CHECK(cli::parse_datetime("2026-11-05 12:00:30") == kFixtureNow);
    CHECK(cli::parse_datetime("2026-11-05T17:30+05:30") == 1'793'880'000);
    CHECK(cli::parse_datetime("2026-11-05T12:00Z") == 1'793'880'000);
    CHECK(cli::parse_datetime("2026-11-05T07:00-0500") == 1'793'880'000);
    for (const char* bad : {"2026-02-30", "2026-13-01", "2026-11-05T24:00", "2026-11-05T12",
                            "05/11/2026", "2026-11-05T12:00 UTC", "", "2026-11-5"})
        CHECK(!cli::parse_datetime(bad));

    // The tier a range reads.
    const std::int64_t n = kFixtureNow;
    CHECK(cli::range_res(n - 1800, n) == 1);
    CHECK(cli::range_res(n - 3600, n) == 60);
    CHECK(cli::range_res(n - 29 * 86400, n) == 60);
    CHECK(cli::range_res(n - 30 * 86400, n) == 3600);
    CHECK(cli::range_res(n - 400 * 86400, n) == 86400);

    // Periods resolve against the clock; cycle against the renewal day.
    const auto period = [&](std::vector<std::string> a, int day = 1) {
        return cli::resolve_period(*parse(std::move(a)), n, day, &why);
    };
    CHECK(period({"apps"})->from == n - 86400 && period({"apps"})->res == 60);
    CHECK(period({"apps", "--period", "5m"})->res == 1);
    CHECK(period({"apps", "--period", "30d"})->res == 3600);
    CHECK(period({"apps", "--period", "cycle"}, 15)->from == 1'792'022'400);   // 15 Oct
    CHECK(!period({"apps", "--from", "2027-01-01"}));
}

// Runs the CLI on a pseudo-terminal, as a person at a shell would.
std::string run_pty(const std::vector<std::string>& args, bool no_color) {
    int master = -1;
    const pid_t pid = ::forkpty(&master, nullptr, nullptr, nullptr);
    if (pid == 0) {
        std::vector<std::string> env{"TZ=UTC", "TERM=xterm", "BANDSIGHT_CLI_NOW=" + std::to_string(kFixtureNow)};
        if (no_color) env.push_back("NO_COLOR=1");
        std::vector<char*> envp, argv;
        for (auto& e : env) envp.push_back(e.data());
        envp.push_back(nullptr);
        std::vector<std::string> a{g_cli};
        a.insert(a.end(), args.begin(), args.end());
        for (auto& x : a) argv.push_back(x.data());
        argv.push_back(nullptr);
        ::execve(g_cli.c_str(), argv.data(), envp.data());
        ::_exit(98);
    }
    std::string s;
    char buf[4096];
    ssize_t n;
    while ((n = ::read(master, buf, sizeof buf)) > 0) s.append(buf, std::size_t(n));   // EIO at the end
    ::close(master);
    ::waitpid(pid, nullptr, 0);
    return s;
}

void terminal_checks(const std::string& db) {
    // Colour and bars on a terminal...
    const std::string tty = run_pty({"apps", "--db", db}, false);
    CHECK(tty.find("\033[1mAPPLICATION") != std::string::npos);
    CHECK(tty.find("\033[34m\u2588") != std::string::npos);   // a download bar
    CHECK(tty.find("\033[2mUnattributed") != std::string::npos);
    // ...never with NO_COLOR, never in a pipe, never in JSON.
    const std::string plain = run_pty({"apps", "--db", db}, true);
    CHECK(plain.find('\033') == std::string::npos && plain.find("\u2588") == std::string::npos);
    CHECK(plain.find("Firefox") != std::string::npos);
    CHECK(run({"apps", "--db", db}, false, {}).out.find('\033') == std::string::npos);
    const std::string json = run_pty({"apps", "--json", "--db", db}, false);
    CHECK(json.find('\033') == std::string::npos && json.find("\"command\":\"apps\"") != std::string::npos);
}

// --- top -------------------------------------------------------------------------

// `bandsight-cli top` on a pseudo-terminal this test holds both ends of, so it
// can type, resize, signal, and read the terminal's settings after the program
// has gone.
class PtyChild {
public:
    PtyChild(const std::vector<std::string>& args, const std::vector<std::string>& extra_env = {},
             int cols = 80, int rows = 24) {
        winsize ws{};
        ws.ws_col = static_cast<unsigned short>(cols);
        ws.ws_row = static_cast<unsigned short>(rows);
        CHECK(::openpty(&master_, &slave_, nullptr, nullptr, &ws) == 0);
        ::tcgetattr(slave_, &before_);
        pid_ = ::fork();
        if (pid_ == 0) {
            ::setsid();
            ::ioctl(slave_, TIOCSCTTY, 0);
            ::dup2(slave_, 0);
            ::dup2(slave_, 1);
            ::dup2(slave_, 2);
            ::close(master_);
            // extra_env first: getenv() takes the first of two with one name.
            std::vector<std::string> env = extra_env;
            for (const char* e : {"TZ=UTC", "NO_COLOR=1", "LC_ALL=C", "TERM=xterm"}) env.push_back(e);
            std::string now = "BANDSIGHT_CLI_NOW=";
            now += std::to_string(kFixtureNow);
            env.push_back(now);
            std::vector<char*> envp, argv;
            for (auto& e : env) envp.push_back(e.data());
            envp.push_back(nullptr);
            std::vector<std::string> a{g_cli, "top"};
            a.insert(a.end(), args.begin(), args.end());
            for (auto& x : a) argv.push_back(x.data());
            argv.push_back(nullptr);
            ::execve(g_cli.c_str(), argv.data(), envp.data());
            ::_exit(98);
        }
    }
    ~PtyChild() {
        if (pid_ > 0 && code_ == -2) {
            ::kill(pid_, SIGKILL);
            ::waitpid(pid_, nullptr, 0);
        }
        ::close(master_);
        ::close(slave_);
    }

    // Reads until `text` has appeared since the last mark(), up to ms.
    bool wait_for(const std::string& text, int ms = 5000) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (out_.find(text, mark_) == std::string::npos) {
            if (std::chrono::steady_clock::now() > deadline) {
                std::fprintf(stderr, "top: no '%s' on the screen\n", text.c_str());
                return false;
            }
            pump(50);
        }
        return true;
    }
    void mark() { mark_ = out_.size(); }
    void type(const std::string& keys) {
        mark();
        CHECK(::write(master_, keys.data(), keys.size()) == ssize_t(keys.size()));
    }
    void resize(int cols, int rows) {
        mark();
        winsize ws{};
        ws.ws_col = static_cast<unsigned short>(cols);
        ws.ws_row = static_cast<unsigned short>(rows);
        ::ioctl(master_, TIOCSWINSZ, &ws);   // the kernel sends SIGWINCH
    }
    void signal(int sig) { ::kill(pid_, sig); }

    // The exit code, -1 for a signal death, -2 if it did not exit in ms.
    int wait_exit(int ms = 5000) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        for (;;) {
            pump(20);
            int status = 0;
            if (::waitpid(pid_, &status, WNOHANG) == pid_) {
                pump(50);
                code_ = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
                return code_;
            }
            if (std::chrono::steady_clock::now() > deadline) return -2;
        }
    }
    const std::string& out() const { return out_; }
    // Settings as they were before the program started, and the screen back.
    bool restored() const {
        termios now{};
        ::tcgetattr(slave_, &now);
        const bool mode = now.c_lflag == before_.c_lflag && now.c_iflag == before_.c_iflag &&
                          now.c_oflag == before_.c_oflag;
        const std::string leave = cli::Terminal::kLeave;
        const bool screen = out_.size() >= leave.size() &&
                            out_.compare(out_.size() - leave.size(), leave.size(), leave) == 0;
        return mode && screen && out_.rfind(cli::Terminal::kEnter, 0) == 0;
    }

private:
    void pump(int ms) {
        pollfd p{master_, POLLIN, 0};
        while (::poll(&p, 1, ms) > 0) {
            char buf[8192];
            const ssize_t n = ::read(master_, buf, sizeof buf);
            if (n <= 0) return;
            out_.append(buf, std::size_t(n));
            ms = 0;
        }
    }

    int master_ = -1, slave_ = -1;
    pid_t pid_ = -1;
    int code_ = -2;
    termios before_{};
    std::string out_;
    std::size_t mark_ = 0;
};

void top_checks(const std::string& db) {
    const std::string sock = g_tmp + "/top-sock";
    const std::string hello = encode_hello(db, {{2, "eth0", "physical"}, {3, "wlan0", "wifi"}}, "0.5.0");
    FakeDaemon d(sock, hello, recorded_samples(10));
    {
        // The database the daemon names, live rates, then q.
        PtyChild t({"--socket", sock});
        CHECK(t.wait_for("bandsightd 0.5.0 running"));
        CHECK(t.wait_for("Now     down"));
        CHECK(t.wait_for("> 1 Applications  last 24 hours"));
        CHECK(t.wait_for("Firefox"));
        CHECK(t.wait_for("units (Mb/s)"));
        t.type("G");   // the end of the list, where Unattributed is
        CHECK(t.wait_for("Unattributed"));
        t.type("q");
        CHECK(t.wait_exit() == 0);
        CHECK(t.restored());
    }
    {
        // Keys: period, units, focus; then Ctrl-C (a key in raw mode).
        PtyChild t({"--socket", sock});
        CHECK(t.wait_for("last 24 hours"));
        t.type("p");
        CHECK(t.wait_for("last 7 days"));
        CHECK(t.wait_for("period (7d)"));
        t.type("b");
        CHECK(t.wait_for("units (MB/s)"));
        t.type("3");
        CHECK(t.wait_for("> 3 Traffic types"));
        t.type("\t");
        CHECK(t.wait_for("> 1 Applications"));
        t.type("\033[B");   // Down, as the terminal sends it
        CHECK(t.wait_for("2-4 of"));
        t.type("\x03");
        CHECK(t.wait_exit() == 0);
        CHECK(t.restored());
    }
    {
        // A resize redraws at the new size; SIGINT from outside ends it.
        PtyChild t({"--db", db, "--socket", sock});
        CHECK(t.wait_for("1 Applications"));
        t.resize(100, 30);
        CHECK(t.wait_for("\033[30;1H"));
        CHECK(t.wait_for("q quit"));
        t.signal(SIGINT);
        CHECK(t.wait_exit() == 0);
        CHECK(t.restored());
    }
    {
        // SIGTERM and SIGHUP restore the terminal too.
        for (const int sig : {SIGTERM, SIGHUP}) {
            PtyChild t({"--db", db, "--socket", sock});
            CHECK(t.wait_for("1 Applications"));
            t.signal(sig);
            CHECK(t.wait_exit() == 0);
            CHECK(t.restored());
        }
    }
    // Read only: across all of them, not one byte reached the daemon.
    CHECK(d.clients() >= 5);
    CHECK(d.bytes_received() == 0);

    {
        // The daemon down: the header says so, the history still works.
        PtyChild t({"--db", db, "--socket", g_tmp + "/none", "--period", "30d"});
        CHECK(t.wait_for("bandsightd is not running - history only"));
        CHECK(t.wait_for("last 30 days"));
        CHECK(t.wait_for("Firefox"));
        CHECK(t.wait_for("no live data while bandsightd is not running"));
        t.type("q");
        CHECK(t.wait_exit() == 0);
        CHECK(t.restored());
    }
    {
        // A missing database: said on screen, and q still works. Wide, so the
        // message (which carries the temp path, longer with a 7-digit pid)
        // stays on one line for wait_for().
        PtyChild t({"--db", g_tmp + "/missing.db", "--socket", g_tmp + "/none"}, {}, 200, 24);
        CHECK(t.wait_for("the file does not exist"));
        t.type("q");
        CHECK(t.wait_exit() == 0);
        CHECK(t.restored());
    }
    {
        // A UTF-8 locale gets unicode; colour only without NO_COLOR.
        PtyChild u({"--db", db, "--socket", g_tmp + "/none"}, {"LC_ALL=C.UTF-8"});
        CHECK(u.wait_for("▶ 1 Applications"));
        CHECK(u.out().find("\033[34m") == std::string::npos);
        u.type("q");
        CHECK(u.wait_exit() == 0);
        PtyChild a({"--db", db, "--socket", g_tmp + "/none", "--ascii"}, {"LC_ALL=C.UTF-8"});
        CHECK(a.wait_for("> 1 Applications"));
        a.type("q");
        CHECK(a.wait_exit() == 0);
        CHECK(a.out().find("█") == std::string::npos);
    }
    // Not a terminal: a message and exit 2, nothing drawn.
    golden("top, not a terminal", {"top", "--db", db}, 2);
    golden("top, a period it does not step through", {"top", "--period", "30m"}, 2);
    golden("--ascii on another command", {"apps", "--ascii"}, 2);
    golden("top, --json", {"top", "--json"}, 2);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <bandsight-cli> <golden file>\n", argv[0]);
        return 2;
    }
    g_cli = argv[1];
    const std::string golden_path = argv[2];
    ::setenv("TZ", "UTC", 1);
    ::tzset();
    g_tmp = std::filesystem::temp_directory_path() / ("bandsight-cli-test-" + std::to_string(::getpid()));
    std::filesystem::create_directories(g_tmp);
    ::chmod(g_tmp.c_str(), 0755);
    const std::string db = g_tmp + "/fixture.db";
    fixture::write(db);
    std::filesystem::copy_file(g_cli, g_tmp + "/bandsight-cli");
    ::chmod((g_tmp + "/bandsight-cli").c_str(), 0755);

    parsing();

    golden("apps, default period", {"apps", "--db", db}, 0);
    golden("apps, 7 days, folded", {"apps", "--db", db, "--period", "7d", "--limit", "3"}, 0);
    golden("apps, 30 days, binary units", {"apps", "--db", db, "--period", "30d", "--binary"}, 0);
    golden("apps, billing cycle", {"apps", "--db", db, "--period", "cycle"}, 0);
    golden("apps, explicit range", {"apps", "--db", db, "--from", "2026-11-05 06:00", "--to", "2026-11-05T11:00"}, 0);
    golden("apps, 5 minutes (per-second rows)", {"apps", "--db", db, "--period", "5m"}, 0);
    golden("apps, nothing in the period", {"apps", "--db", db, "--from", "2026-01-01", "--to", "2026-01-02"}, 0);
    golden("hosts", {"hosts", "--db", db}, 0);
    golden("hosts, one application", {"hosts", "--db", db, "--app", "node", "--period", "7d"}, 0);
    golden("hosts, an application with no traffic", {"hosts", "--db", db, "--app", "nosuch"}, 0);
    golden("hosts, 30 days, folded", {"hosts", "--db", db, "--period", "30d", "--limit", "5"}, 0);
    golden("types", {"types", "--db", db}, 0);
    golden("types, 30 days (recording began inside it)", {"types", "--db", db, "--period", "30d"}, 0);
    golden("ifaces", {"ifaces", "--db", db}, 0);
    golden("ifaces, all", {"ifaces", "--db", db, "--all", "--period", "7d"}, 0);
    golden("status, daemon not running", {"status", "--db", db, "--socket", g_tmp + "/none"}, 0);
    {
        // Without --db and with the daemon down, the default database is read
        // (whether this machine has one decides the exit code) and the reader
        // is told the history may be stale.
        const Result r = run({"apps", "--socket", g_tmp + "/none"});
        CHECK(r.err.find("bandsightd is not running; showing history only.") == 0);
    }

    // A database an old daemon wrote: no hosts by interface, no traffic types.
    const std::string old = g_tmp + "/v2.db";
    std::filesystem::copy_file(db, old);
    {
        sqlite3* h = nullptr;
        sqlite3_open(old.c_str(), &h);
        CHECK(sqlite3_exec(h,
                           "DROP TABLE type_samples;"
                           "CREATE TABLE h2 AS SELECT res, ts, app_id, host_id, rx, tx FROM app_host_samples;"
                           "DROP TABLE app_host_samples; ALTER TABLE h2 RENAME TO app_host_samples;",
                           nullptr, nullptr, nullptr) == SQLITE_OK);
        sqlite3_close(h);
    }
    golden("types, old database", {"types", "--db", old}, 0);
    golden("hosts, old database", {"hosts", "--db", old}, 0);

    // Errors, in plain words, exit 1.
    golden("missing database", {"apps", "--db", g_tmp + "/missing.db"}, 1);
    const std::string junk = g_tmp + "/junk.db";
    std::ofstream(junk) << std::string(4096, 'x');
    golden("not a database", {"ifaces", "--db", junk}, 1);
    const std::string locked = g_tmp + "/locked.db";
    std::filesystem::copy_file(db, locked);
    ::chmod(locked.c_str(), 0600);
    if (::geteuid() == 0 || ::access(locked.c_str(), R_OK) != 0)
        golden("unreadable database", {"status", "--db", locked, "--socket", g_tmp + "/none"}, 1, true);

    // Usage mistakes, exit 2.
    golden("unknown command", {"frobnicate"}, 2);
    golden("unknown period", {"apps", "--period", "2w"}, 2);
    golden("option for another command", {"types", "--app", "firefox"}, 2);
    golden("--to without --from", {"hosts", "--to", "2026-11-05"}, 2);
    golden("bad date", {"apps", "--from", "5 Nov"}, 2);
    golden("no command", {}, 2);

    daemon_checks(db);
    json_checks(db);
    terminal_checks(db);
    top_checks(db);

    // Help and version: complete, exit 0.
    golden("help", {"--help"}, 0);
    for (const char* c : {"status", "apps", "hosts", "types", "ifaces", "live", "top"})
        golden(std::string(c) + " help", {c, "--help"}, 0);
    {
        const Result v = run({"--version"});
        CHECK(v.code == 0 && v.out == "bandsight-cli " BANDSIGHT_TEST_PROJECT_VERSION "\n");
    }

    if (std::getenv("BANDSIGHT_UPDATE_GOLDEN")) {
        std::ofstream(golden_path) << g_golden;
        std::fprintf(stderr, "wrote %s\n", golden_path.c_str());
    } else {
        std::ifstream in(golden_path);
        std::stringstream want;
        want << in.rdbuf();
        if (want.str() != g_golden) {
            CHECK(want.str() == g_golden);
            const std::string got = g_tmp + "/golden.actual";
            std::ofstream(got) << g_golden;
            std::fprintf(stderr, "output differs from %s; this run's is in %s:\n",
                         golden_path.c_str(), got.c_str());
            if (std::system(("diff -u '" + golden_path + "' '" + got + "' | head -60 >&2").c_str()) < 0) std::fprintf(stderr, "(diff failed)\n");
            return 1;   // keep the actual output for inspection
        }
    }
    std::filesystem::remove_all(g_tmp);
    return g_failures == 0 ? 0 : 1;
}
