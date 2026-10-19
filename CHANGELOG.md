# Changelog

## [1.1.0] - 2026-10-19

### Added
- Multi-device menu with hardware serial display.
- Select one device or all devices.
- Parallel processing via fork() for "all devices".
- Hardware serial matching for Download Mode detection, with
  PID-only fallback after 5 s grace period.

### Changed
- `find_samsung_port()` replaced by `find_samsung_ports()` (list).
- `process_device()` extracted from `main()` for reuse.

## [1.0.2] - 2026-10-18

### Changed
- Uniform 30 s timeout for Download Mode detection.

### Fixed
- Inconsistent wait times between the two reboot paths.

## [1.0.1] - 2026-10-18

### Added
- Confirm Download Mode via sysfs PID detection.
- Ctrl+C aborts the wait loop cleanly.

### Fixed
- Treat `EIO`/`ENODEV`/`ENOENT` after the first `AT` as success.

## [1.0.0] - 2026-10-18

### Added
- Initial Linux-only release.
- Auto-detect Samsung serial ports via sysfs `idVendor`.
- Send AT command sequence to force Download Mode.
