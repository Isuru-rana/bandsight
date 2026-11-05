# Changelog

What each release added, changed, fixed and removed. The section for a version
is also the text of its [GitHub release](https://github.com/Isuru-rana/bandsight/releases).

## [0.6.0] - 2026-11-05

### Added
- `bandsight-cli`: terminal client for headless machines, no Qt
- `bandsight-cli` commands: `status`, `apps`, `hosts`, `types`, `ifaces`, `live`
- Periods `5m` to `30d`, `cycle`, `--from`/`--to`
- `--json` on every command: raw byte counts, ISO 8601 times
- `--bytes`, `--binary`, `--limit`, `--app`, `--all`, `--interval`, `--count`
- Colour and bars on a terminal only; `NO_COLOR` honoured
- Exit codes: 0 ok, 1 runtime error, 2 usage error
- Shared Qt-free query library (`core/`) used by the GUI and the CLI
- `bandsight-cli top`: full-screen live view, no new dependencies
- `top` header: daemon status and version, live rates, cycle used/cap/projection
- `top` sparkline of the last 60 s, unicode with an ASCII fallback (`--ascii`)
- `top` panels: Applications, Hosts, Traffic types with bars and Unattributed rows
- `top` keys: `1`/`2`/`3`/`Tab` focus, `p` period, `b` Mb/s or MB/s, arrows or `j`/`k` scroll, `q` quit
- `top` refresh: live rates every second (read only), history every 5 seconds
- `top` keeps showing history while the daemon is down, and reconnects
- `top` redraws on resize and restores the terminal on every exit
- `top` exits 2 when not on a terminal; `NO_COLOR` honoured

### Changed
- GUI history queries run on the shared library; same SQL, same results

## [0.5.0] - 2026-11-05

### Added
- Settings button in the main window, and `Ctrl+,`
- Settings sections: General, Display, About
- Themes: System, Light, Dark, High contrast, Sand, Slate
- About: GUI and daemon version, database path, Qt, build type, commit, "Copy details"
- `bandsightd --version` and `--help`, `bandsight --version`
- Optional `version` field in the hello frame
- Traffic colours: Default, Blue / Orange, Teal / Magenta, Green / Purple, Blue / Yellow, Custom
- Warning when a custom traffic colour is hard to see or hard to tell apart

### Changed
- bandsightd refuses a database path that is a symlink
- bandsightd exits with status 2 on an unknown option or a flag without a value
- Unit and theme changes apply immediately; Cancel reverts them
- Speed unit choices spelled out: Mb/s megabits, MB/s megabytes
- Bit rates shown as kb/s, Mb/s, Gb/s (was kbit/s, Mbit/s, Gbit/s)
- Settings OK contacts bandsightd only when the cap or cycle day changed
- Light theme upload colour one step darker, for 3:1 against the chart background
- Release builds run the tests before packaging

### Fixed
- A unit change left the data cap bar, Interfaces table, host tooltips and tray tooltip in the old unit until the next refresh
- bandsightd given an unknown option or `--db` without a path started on the default database
- Group members could make bandsightd chmod or overwrite files via symlinks in its state directory
- An app updated while running split into a second "(deleted)" app
- Data cap counting one hour twice (or missing it) the day after a DST change

## [0.4.0] - 2026-11-03

### Added
- Traffic type card on the Usage tab: bytes by service (HTTPS, QUIC, DNS, SSH, …)
- Local port recorded per socket
- Database table `type_samples`

### Removed
- "Needs port and protocol collection" placeholder

## [0.3.1] - 2026-11-03

### Added
- "Unattributed" row in Apps and Hosts when they cover less than the total
- Log line when the per-app BPF map is full
- Unit tests (`ctest`)

### Changed
- Control socket: at most 64 clients, extra connections closed
- Container traffic kept apart from unknown-interface traffic
- List labels shortened in the middle, full name in every tooltip

### Fixed
- Apps and Hosts missing most traffic of unconnected UDP (calls, Tailscale, QUIC)
- Two different apps or hosts shown with identical truncated names
- UDP sent by dual-stack (IPv6) sockets to IPv4 addresses counted twice
- Whole total shown as Unattributed when the app query failed
- Wrong Unattributed amount flashing while switching the period
- Daemon stalls (no counting) while one client floods commands
- 100% CPU and log flood when the daemon runs out of file descriptors
- Command lost when the client half-closes right after sending
- Settings change reported as saved when the database write failed
- Live graph repeats the same traffic while the database is unwritable
- "Deltas discarded" log line every second during a long disk-full
- deb/rpm upgrade re-enables and starts a disabled bandsightd

## [0.3.0] - 2026-10-31

### Added
- Install and update via apt (signed repo at isuru-rana.github.io/bandsight)
- Rollback to any release: `apt install bandsight=<version>`
- Host names in Hosts column (from systemd-resolved lookups)
- LAN / WAN tags on hosts
- WAN / LAN bars on Graph totals card
- "Others" row in Apps and Hosts lists, hover for the hidden entries

### Changed
- Second launch raises the running window, no second copy
- Interfaces tab: each interface listed once (incl. replugged USB adapters)

### Fixed
- 100% CPU on one core with Usage tab open
- Unreadable row tooltips in Usage lists
- Usage lists looked cut off, no sign of more rows

### Removed
- Gone interfaces (old Docker veths, unplugged adapters) from Interfaces tab, unless used in the period
- Scrolling in Usage lists (replaced by "others" row)

### Notes
- Host names need systemd-resolved; others show as IP

## [0.2.0] - 2026-10-27

### Added
- Hosts column: bytes per remote IP, per app

### Changed
- Database schema v3 (migrated automatically)

### Fixed
- Container traffic counted against a physical interface
- `.deb` build on Debian 12 (Qt 6.4)

### Removed
- Separate Arch debug package

### Notes
- Per-app history before 0.2.0 may include container traffic; totals, graph and cap unaffected

## [0.1.0] - 2026-10-02

### Added
- bandsightd eBPF daemon with SQLite history
- Qt 6 viewer: Graph, Usage, Interfaces tabs
- Tray icon, data cap, billing cycle
- `.deb`, `.rpm` and Arch packages on every version tag
