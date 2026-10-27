# Changelog

All notable changes to this project are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- CodeQL code scanning (security-extended queries) of the C++ from a real macOS build and of the
  workflows, on every pull request, push to `main` and weekly.

## [0.7.0] - 2026-10-27

First public release. Versions 0.1 to 0.6 were shared privately for testing.

### Added

- Settings for detection and limits, each with hard bounds: `--detect-window`, `--detect-attaches`,
  `--verify-timeout`, `--cooldown`, `--max-resets-per-hour` and `--failure-pause`. The installer
  passes them to the service, and the values in use are logged at start.
- `--log-level` (`error`, `warning`, `info`, `debug`) and `--log-file`. Debug logging adds guard
  changes and state-file saves.
- `--help`, with each setting's range and default.
- Prebuilt release archives with SHA-256 checksums and build provenance attestations, and a
  one-line installer, so installing no longer needs Xcode.
- CMake build with presets, GoogleTest unit tests, and CI for build, tests, clang-format,
  clang-tidy (C++ Core Guidelines) and ShellCheck.

### Changed

- Rewritten in modern C++20 with RAII wrappers for every IOKit and Core Foundation handle.
- Logging uses spdlog. Each line now includes its level, the service writes `events.log` itself,
  and the rotated file is `events.1.log`.
- Argument parsing uses CLI11. Error messages name the problem and exit with code 2.

### Removed

- The battery guard. A stuck dock doesn't charge the Mac, so the guard blocked recovery when it was
  needed most.

### Fixed

- If macOS opened the controller interface without returning it, the restart was reported as sent.
  It is now reported as an error.
- Detection could count an attach from just before the window, so 3 attaches up to one poll
  interval more than 2 s apart could trigger a restart. Only attaches seen between polls inside the
  window count now. Counting this strictly, a 2 s window missed a 0.91 s loop whenever polls ran
  even 1 ms late, so the default window is now 2.5 s. It catches loops up to 1.1 s per attach with
  polls up to 300 ms apart and never fires on attaches 1.25 s or more apart.
- A failure pause longer than an hour (`--failure-pause` above 3600) ended early if the watcher
  restarted during it. The state file now keeps each port's last restart time at any age.

[Unreleased]: https://github.com/dillonm197/d6000-dock-recovery/compare/v0.7.0...HEAD
[0.7.0]: https://github.com/dillonm197/d6000-dock-recovery/releases/tag/v0.7.0
