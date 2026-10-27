# Security

This tool runs as root and sends a command to USB-C controller hardware, so security reports are
taken seriously.

## Reporting a vulnerability

**Please don't report security vulnerabilities through public GitHub issues.**

Report them privately through GitHub instead: open the repository's **Security** tab and choose
**Report a vulnerability**. Please include:

- what the issue is and what an attacker could do with it,
- the steps or code to reproduce it,
- the version (`d6000-recovery --version`), Mac model and macOS version.

You'll get an acknowledgement as soon as the maintainer sees it. Please give a reasonable time for
a fix before disclosing it publicly.

## Supported versions

Only the latest release gets security fixes.

## Scope

In scope: the helper, the installer and uninstaller, the download script, and the release workflow.
Issues in macOS, the controller firmware, or the dock itself should go to Apple or Dell.
