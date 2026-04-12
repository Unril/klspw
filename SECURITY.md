# Security Policy

## Reporting a Vulnerability

If you discover a security vulnerability in klspw, please report it responsibly.

Email: [open a GitHub Security Advisory](https://github.com/Unril/klspw/security/advisories/new)

Please include:

- A description of the vulnerability
- Steps to reproduce
- Affected versions

You can expect an initial response within 7 days. We aim to release a fix within 30 days of confirming the vulnerability, depending on complexity.

## Supported Versions

Only the latest release is supported with security updates.

## Scope

klspw is a CLI tool that runs Gradle builds and generates workspace.json files. Security-relevant areas include:

- Subprocess execution (Gradle invocation via reproc++)
- File I/O (config reading, workspace.json writing)
- Init script injection into Gradle builds
