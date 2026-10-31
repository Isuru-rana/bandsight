# Changelog

What each release added, changed, fixed and removed. The section for a version
is also the text of its [GitHub release](https://github.com/Isuru-rana/bandsight/releases).

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
