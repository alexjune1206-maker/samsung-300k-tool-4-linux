# Changelog

## [1.0.2] - 2026-10-18
- Uniform 30 s timeout for Download Mode detection.
- Fixed inconsistent wait times between the two reboot paths.

## [1.0.1] - 2026-10-18
- Confirm Download Mode via sysfs PID detection.
- Ctrl+C aborts the wait loop cleanly.
- Treat `EIO`/`ENODEV`/`ENOENT` after the first `AT` as success.

## [1.0.0] - 2026-10-18
- Initial Linux-only release.
- Auto-detect Samsung serial ports via sysfs `idVendor`.
- Send AT command sequence to force Download Mode.
