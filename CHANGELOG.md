# Changelog

What each release added, changed, fixed and removed. The section for a version
is also the text of its [GitHub release](https://github.com/Isuru-rana/bandsight/releases).

## [0.3.0] - 2026-10-31

### Added
- **Install and update with apt.** A signed package source at
  `https://isuru-rana.github.io/bandsight`: add it once (see Install in the
  README), then `sudo apt install bandsight` and `sudo apt upgrade` work like any
  other package. Every release stays available, so `apt install bandsight=<version>`
  goes back to one.
- **Host names in the Hosts column.** bandsightd follows systemd-resolved's
  lookups and names each host by the domain an app asked for
  (`www.youtube.com` instead of `142.250.183.14`). The IP stays in the tooltip.
- **LAN / WAN tags.** Hosts are marked LAN or WAN, and the Graph totals card
  splits the window's traffic into WAN and LAN bars.
- **"Others" row in the Usage lists.** Apps and hosts that do not fit the card
  are summed into one row ("16 others") with its total and bar; hovering it
  lists them.

### Changed
- **One window per user.** Launching Bandsight again brings the running window
  to the front instead of starting a second copy with a second tray icon.
- **Interfaces tab lists each interface once,** even a USB adapter that was
  plugged in several times.

### Fixed
- 100% CPU on one core while the Usage tab was open (the cards repainted
  themselves endlessly).
- Row tooltips in the Usage lists had no background and could not be read.
- The Usage lists looked cut off at the card's edge, with no sign there was more.

### Removed
- Interfaces that no longer exist (old Docker veths, unplugged adapters) are no
  longer listed unless they moved data in the selected period.
- The Usage lists no longer scroll; the "others" row replaces it.

### Notes
- Host names need systemd-resolved. Hosts nobody looked up through it (LAN
  devices, direct-IP connections, apps with their own encrypted DNS) still show
  as IP addresses.

## [0.2.0] - 2026-10-27

### Added
- **Hosts column** on the Usage tab: bytes per remote IP address, per app.

### Fixed
- Traffic between containers was counted against a physical interface
  (interface numbers are per network namespace). Per-app history recorded
  before 0.2.0 may still include it; interface totals, the graph and the data
  cap were never affected.
- The `.deb` failed to build on Debian 12 (Qt 6.4).

### Changed
- The database schema moved to version 3; it is migrated automatically.

### Removed
- The separate Arch debug package.

## [0.1.0] - 2026-10-02

### Added
- First public release: the bandsightd eBPF daemon with SQLite history, and
  the Qt 6 desktop viewer with Graph, Usage and Interfaces tabs, tray icon, data
  cap and billing cycle.
- `.deb`, `.rpm` and Arch packages, built on every version tag.
