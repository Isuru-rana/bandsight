// SPDX-License-Identifier: GPL-3.0-or-later
// How a process becomes an application: the executable path the kernel
// reports, and the label derived from it.
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>

#include "app_name.h"
#include "check.h"
#include "resolver.h"

using namespace bandsight;

namespace {

void exe_identity_strips_deleted() {
    CHECK(exe_identity("/usr/lib/firefox/firefox (deleted)") == "/usr/lib/firefox/firefox");
    CHECK(exe_identity("/usr/bin/curl") == "/usr/bin/curl");
    CHECK(exe_identity("") == "");
    // Only the kernel's suffix, only at the end.
    CHECK(exe_identity("/opt/a (deleted)/b") == "/opt/a (deleted)/b");
    CHECK(exe_identity(" (deleted)") == " (deleted)");
    CHECK(exe_identity("/x (deleted) (deleted)") == "/x (deleted)");
}

// The real thing: a program whose file is replaced while it runs, as a package
// upgrade does, keeps its identity.
void running_program_after_upgrade() {
    namespace fs = std::filesystem;
    char tmpl[] = "/tmp/bandsight-test-identity-XXXXXX";
    const char* dir = ::mkdtemp(tmpl);
    CHECK(dir != nullptr);
    if (!dir) return;
    const std::string exe = std::string(dir) + "/sleeper";
    std::error_code ec;
    fs::copy_file("/bin/sleep", exe, ec);
    CHECK(!ec);
    if (ec) return;
    fs::permissions(exe, fs::perms::owner_all);

    const pid_t pid = ::fork();
    if (pid == 0) {
        ::execl(exe.c_str(), exe.c_str(), "30", static_cast<char*>(nullptr));
        std::_Exit(127);
    }
    // Wait for the exec to have happened.
    for (int i = 0; i < 100; ++i) {
        std::error_code e2;
        if (fs::read_symlink("/proc/" + std::to_string(pid) + "/exe", e2) == exe) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    fs::remove(exe, ec);   // the upgrade

    Resolver r;   // a fresh daemon: nothing cached, so /proc is read now
    CHECK(r.exe_for(static_cast<std::uint32_t>(pid)) == exe);

    ::kill(pid, SIGKILL);
    ::waitpid(pid, nullptr, 0);
    fs::remove_all(dir, ec);
}

void app_names() {
    // The common case: the basename.
    CHECK(app_name_from_exe("/usr/bin/curl") == "curl");
    CHECK(app_name_from_exe("/usr/lib/firefox/firefox") == "firefox");
    CHECK(app_name_from_exe("python3.14") == "python3.14");
    // A bare version or wrapper directory names nothing: walk up.
    CHECK(app_name_from_exe("/home/u/.local/share/claude/versions/2.1.247") == "claude");
    CHECK(app_name_from_exe("/opt/tool/v3/bin") == "tool");
    CHECK(app_name_from_exe("/opt/tool/current/20240115") == "tool");
    CHECK(app_name_from_exe("/srv/app/current/20240115") == "20240115");   // "app" is a root
    // Walking out of the install into a filesystem root keeps the basename.
    CHECK(app_name_from_exe("/usr/bin/1.0") == "1.0");
    CHECK(app_name_from_exe("/opt/2.0") == "2.0");
    // Degenerate input.
    CHECK(app_name_from_exe("") == "");
    CHECK(app_name_from_exe("/") == "/");
    CHECK(app_name_from_exe("unknown") == "unknown");
    CHECK(app_name_from_exe("/a//b/./c") == "c");
    // "v" alone and names with letters are names, not versions.
    CHECK(app_name_from_exe("/opt/x/v") == "v");
    CHECK(app_name_from_exe("/usr/bin/node20") == "node20");
}

}  // namespace

int main() {
    exe_identity_strips_deleted();
    running_program_after_upgrade();
    app_names();
    if (g_failures) std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    else std::printf("test_identity: all checks passed\n");
    return g_failures ? 1 : 0;
}
