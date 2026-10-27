# Contributing

Thanks for helping. The most useful contribution is a report from a Mac or dock that hasn't been
tested yet, whether the tool worked or not: open one of the
[issue forms](https://github.com/dillonm197/d6000-dock-recovery/issues/new/choose) and include the
log it asks for. Bug fixes, documentation and code changes are welcome too.

## Reporting

- **Compatibility report:** what happened on your Mac and dock, with `--list` output and the log.
- **False positive:** the tool restarted a port when nothing was wrong. Include the log lines from a
  minute before to a minute after the restart, and what you were doing.
- **Bug:** anything else that doesn't work as documented.
- **Security issue:** don't open an issue; follow [SECURITY.md](SECURITY.md).

## Development setup

You need an Apple-silicon Mac, the Xcode Command Line Tools and a few Homebrew packages:

```bash
xcode-select --install
brew install cmake llvm shellcheck
```

Build and run the tests (the debug preset treats warnings as errors):

```bash
cmake --workflow --preset debug
```

Run the same checks as CI before you open a pull request:

```bash
"$(brew --prefix llvm)/bin/clang-format" --dry-run --Werror src/*.cpp src/*.hpp tests/*.cpp
"$(brew --prefix llvm)/bin/run-clang-tidy" -clang-tidy-binary "$(brew --prefix llvm)/bin/clang-tidy" \
    -p build/debug -quiet "$PWD/src/.*\.cpp$"
shellcheck -o all -e SC2250,SC2312 scripts/*.sh
```

## Standards

- **C++20**, following the [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines),
  enforced by clang-tidy's `cppcoreguidelines-*` checks plus `bugprone`, `modernize`,
  `performance` and `readability` ([.clang-tidy](.clang-tidy)). Every finding fails CI. If a rule
  really doesn't fit a line (for example a COM-style cast at the IOKit boundary), suppress it on
  that line with `NOLINT(check-name)` and a reason.
- **Formatting:** clang-format's Microsoft style ([.clang-format](.clang-format)).
- **Shell:** Bash with `set -euo pipefail`, clean under ShellCheck with all optional checks.
- **Comments** explain why, not what. Every source file starts with the copyright and SPDX lines.
- **Safety first:** any change to detection, guards or limits needs a unit test, and must keep the
  guarantees in [How it works](docs/how-it-works.md). Code that talks to the controller stays in
  `src/controller.cpp`.

## Testing on hardware

Unit tests cover the policy, settings and state file without hardware. To watch the real logic
without sending anything, run a dry run as yourself:

```bash
build/debug/d6000-recovery --watch --all-ports --dry-run --log-level debug
```

A live test needs a D6000 and patience: replug until the light stays amber, and the log shows
whether the watcher caught it. Mention the Mac model and macOS version in your pull request.

## Pull requests

- Keep each pull request to one change, and describe why it's needed.
- Update the docs and [CHANGELOG.md](CHANGELOG.md) when behaviour changes.
- CI must pass.

## License

By contributing, you agree that your contributions are licensed under the
[Apache License 2.0](LICENSE), the same as the project.
