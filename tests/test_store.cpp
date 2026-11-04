// SPDX-License-Identifier: GPL-3.0-or-later
// Store's handling of the files it is pointed at. The state directory is
// writable by group bandsight while the daemon runs as root, so nothing in it
// may be followed as a symlink.
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "check.h"
#include "store.h"

using namespace bandsight;

namespace {

std::string make_dir() {
    char tmpl[] = "/tmp/bandsight-test-store-XXXXXX";
    const char* d = ::mkdtemp(tmpl);
    return d ? d : "";
}

mode_t mode_of(const std::string& p) {
    struct stat st{};
    return ::stat(p.c_str(), &st) == 0 ? (st.st_mode & 07777) : 0;
}

off_t size_of(const std::string& p) {
    struct stat st{};
    return ::stat(p.c_str(), &st) == 0 ? st.st_size : -1;
}

void write_file(const std::string& p, const char* text, mode_t mode) {
    if (FILE* f = std::fopen(p.c_str(), "w")) {
        std::fputs(text, f);
        std::fclose(f);
    }
    ::chmod(p.c_str(), mode);
}

void remove_all(const std::string& dir) {
    for (const char* n : {"/db", "/db-wal", "/db-shm", "/target", "/link.db", "/plain.db",
                          "/plain.db-wal", "/plain.db-shm"})
        ::unlink((dir + n).c_str());
    ::rmdir(dir.c_str());
}

// A database path that is a symlink is refused, and its target is untouched.
void db_symlink_refused() {
    const std::string dir = make_dir();
    CHECK(!dir.empty());
    write_file(dir + "/target", "not a database", 0600);
    CHECK(::symlink((dir + "/target").c_str(), (dir + "/link.db").c_str()) == 0);

    bool threw = false;
    try {
        Store s(dir + "/link.db");
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
    CHECK(mode_of(dir + "/target") == 0600);
    CHECK(size_of(dir + "/target") == 14);
    remove_all(dir);
}

// A planted sidecar symlink does not get its target chmodded.
void sidecar_symlink_not_chmodded() {
    const std::string dir = make_dir();
    CHECK(!dir.empty());
    {
        Store s(dir + "/db");   // a real database first
        s.migrate();
    }
    ::unlink((dir + "/db-wal").c_str());
    ::unlink((dir + "/db-shm").c_str());
    write_file(dir + "/target", "secret", 0600);
    CHECK(::symlink((dir + "/target").c_str(), (dir + "/db-wal").c_str()) == 0);

    try {
        Store s(dir + "/db");
        s.migrate();   // SQLite refuses the symlinked WAL; that may throw
    } catch (const std::exception&) {
    }
    CHECK(mode_of(dir + "/target") == 0600);
    CHECK(size_of(dir + "/target") == 6);
    remove_all(dir);
}

// The ordinary case still works: a plain file is made group read-write.
void plain_db_is_0660() {
    const std::string dir = make_dir();
    CHECK(!dir.empty());
    write_file(dir + "/plain.db", "", 0600);
    {
        Store s(dir + "/plain.db");
        s.migrate();
        CHECK(s.schema_version() == 3);
    }
    CHECK(mode_of(dir + "/plain.db") == 0660);
    remove_all(dir);
}

}  // namespace

int main() {
    db_symlink_refused();
    sidecar_symlink_not_chmodded();
    plain_db_is_0660();
    if (g_failures) std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    else std::printf("test_store: all checks passed\n");
    return g_failures ? 1 : 0;
}
