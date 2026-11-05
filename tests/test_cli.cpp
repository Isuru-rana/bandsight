// SPDX-License-Identifier: GPL-3.0-or-later
// bandsightd's command line, on the real binary (argv[1]): --version and
// --help print and exit 0, a bad command line exits 2, and none of them create
// the database or the socket - they used to start the daemon on the live DB.
#include <dirent.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <string>
#include <vector>

#include "check.h"
#include "cli.h"

namespace {

struct Run {
    int status = -1;   // exit code, or -1 if it did not exit normally
    std::string out, err;
};

std::string drain(int fd) {
    std::string s;
    char buf[4096];
    ssize_t n;
    while ((n = ::read(fd, buf, sizeof buf)) > 0) s.append(buf, std::size_t(n));
    ::close(fd);
    return s;
}

Run run(const char* exe, const std::vector<std::string>& args) {
    int out[2], err[2];
    if (::pipe(out) != 0 || ::pipe(err) != 0) return {};
    const pid_t pid = ::fork();
    if (pid == 0) {
        ::dup2(out[1], 1);
        ::dup2(err[1], 2);
        ::close(out[0]);
        ::close(err[0]);
        std::vector<char*> argv{const_cast<char*>(exe)};
        for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        ::execv(exe, argv.data());
        ::_exit(127);
    }
    ::close(out[1]);
    ::close(err[1]);
    Run r;
    // The outputs are small; reading one then the other cannot deadlock.
    r.out = drain(out[0]);
    r.err = drain(err[0]);
    int st = 0;
    ::waitpid(pid, &st, 0);
    r.status = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    return r;
}

int entries(const std::string& dir) {
    int n = 0;
    if (DIR* d = ::opendir(dir.c_str())) {
        while (dirent* e = ::readdir(d))
            if (std::string(e->d_name) != "." && std::string(e->d_name) != "..") ++n;
        ::closedir(d);
    }
    return n;
}

std::vector<const char*> argv_of(std::initializer_list<const char*> a) {
    std::vector<const char*> v{"bandsightd"};
    v.insert(v.end(), a);
    return v;
}

}  // namespace

int main(int argc, char** argv) {
    using bandsight::CliOptions;
    using bandsight::parse_cli;

    // --- the parser ---------------------------------------------------------
    {
        auto v = argv_of({});
        const CliOptions o = parse_cli(int(v.size()), v.data());
        CHECK(o.action == CliOptions::Action::Run);
        CHECK(o.db_path == "/var/lib/bandsight/bandsight.db");
        CHECK(o.socket_path == "/run/bandsightd/sock");
    }
    {
        auto v = argv_of({"--db", "/tmp/a.db", "--socket", "/tmp/s"});
        const CliOptions o = parse_cli(int(v.size()), v.data());
        CHECK(o.action == CliOptions::Action::Run);
        CHECK(o.db_path == "/tmp/a.db");
        CHECK(o.socket_path == "/tmp/s");
    }
    for (auto args : {argv_of({"--version"}), argv_of({"--db", "/x", "--version"})}) {
        CHECK(parse_cli(int(args.size()), args.data()).action == CliOptions::Action::Version);
    }
    for (auto args : {argv_of({"--help"}), argv_of({"-h"})})
        CHECK(parse_cli(int(args.size()), args.data()).action == CliOptions::Action::Help);
    // A typo, a stray argument, a flag missing its value, or one whose "value"
    // is the next flag: all refused, none starts the daemon on the default DB.
    for (auto args : {argv_of({"--verison"}), argv_of({"run"}), argv_of({"--db"}),
                      argv_of({"--db", ""}), argv_of({"--db", "--socket", "/s"}),
                      argv_of({"--socket"})}) {
        const CliOptions o = parse_cli(int(args.size()), args.data());
        CHECK(o.action == CliOptions::Action::Usage);
        CHECK(!o.error.empty());
    }
    CHECK(bandsight::version_text() == std::string("bandsightd ") + BANDSIGHT_TEST_PROJECT_VERSION);

    // --- the binary ---------------------------------------------------------
    if (argc < 2) {
        std::fprintf(stderr, "usage: test_cli <path to bandsightd>\n");
        return 1;
    }
    const char* exe = argv[1];
    char tmpl[] = "/tmp/bandsight-cli-XXXXXX";
    const char* dir = ::mkdtemp(tmpl);
    CHECK(dir != nullptr);
    if (!dir) return 1;
    const std::string db = std::string(dir) + "/bandsight.db";
    const std::string sock = std::string(dir) + "/sock";

    {
        const Run r = run(exe, {"--db", db, "--socket", sock, "--version"});
        CHECK(r.status == 0);
        CHECK(r.out == std::string("bandsightd ") + BANDSIGHT_TEST_PROJECT_VERSION + "\n");
        CHECK(r.err.empty());
    }
    {
        const Run r = run(exe, {"--version", "--db", db});
        CHECK(r.status == 0);
    }
    {
        const Run r = run(exe, {"--help", "--db", db, "--socket", sock});
        CHECK(r.status == 0);
        CHECK(r.out.find("Usage: bandsightd") != std::string::npos);
        CHECK(r.out.find("--version") != std::string::npos);
    }
    {
        const Run r = run(exe, {"--db", db, "--socket", sock, "--frobnicate"});
        CHECK(r.status == 2);
        CHECK(r.out.empty());
        CHECK(r.err.find("unknown option: --frobnicate") != std::string::npos);
        CHECK(r.err.find("Usage: bandsightd") != std::string::npos);
    }
    {
        const Run r = run(exe, {"--socket", sock, "--db"});
        CHECK(r.status == 2);
        CHECK(r.err.find("--db needs a path") != std::string::npos);
    }
    // Nothing was created: no database, no -wal/-shm, no socket.
    CHECK(entries(dir) == 0);
    ::rmdir(dir);

    if (g_failures) std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    return g_failures ? 1 : 0;
}
