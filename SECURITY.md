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

klspw is a CLI tool that runs Gradle builds and generates workspace.json files.
Security-relevant areas include:

- Subprocess execution (Gradle invocation via reproc++)
- File I/O (config reading, workspace.json writing)
- Init script injection into Gradle builds

## Threat model

### Trust boundaries

- Config file (klspw.yaml): trusted input from the local user. klspw validates
  paths and structure but does not treat config content as adversarial.
- Gradle output (stdout): semi-trusted. klspw parses JSON between known
  delimiters (`KLSPW_BEGIN`/`KLSPW_END`). Malformed output causes parse errors,
  not code execution. Fuzz testing covers this boundary.
- Init script injection: klspw writes a temporary Gradle init script and passes
  it via `--init-script`. The script content is embedded into the binary at
  configure time (via CMake `configure_file`) and is not user-modifiable at
  runtime.
- Filesystem: klspw reads config and classpath jars, writes workspace.json. All
  paths are resolved relative to the config file directory. No network I/O is
  performed by klspw itself (Gradle may access the network during its build).

### Secure design principles applied

- Input validation: config YAML is validated before use (`ValidateContext`).
  Gradle JSON output is parsed with strict delimiters and fuzz-tested.
- Least privilege: klspw does not require elevated permissions. It runs Gradle
  with the same privileges as the invoking user.
- Defense in depth: CI runs AddressSanitizer, UndefinedBehaviorSanitizer, and
  ClusterFuzzLite on every PR. CodeQL scans for common C++ vulnerabilities.
  Compiler warnings (`-Wall -Wextra -Wpedantic`) and clang-tidy are enabled.
- Supply chain: all GitHub Actions are pinned to full commit SHAs. vcpkg is
  pinned to a specific commit. Release binaries include artifact attestations
  for provenance verification.

### Common C++ vulnerability mitigations

| Vulnerability class | Mitigation |
| --- | --- |
| Buffer overflow | ASan in CI, fuzz testing, `std::string`/`std::vector` (no raw buffers) |
| Use-after-free | ASan in CI, RAII ownership throughout |
| Undefined behavior | UBSan in CI, `-Wpedantic`, clang-tidy `bugprone-*` checks |
| Command injection | Subprocess args passed as arrays via reproc++, not shell strings |
| Path traversal | Paths resolved relative to config dir; no user-controlled path concatenation with `..` |
| Integer overflow | No security-critical integer arithmetic; sizes come from STL containers |

### Limitations

- klspw trusts the local filesystem and the user's Gradle installation. A
  compromised Gradle wrapper or build script can produce arbitrary output.
- No sandboxing is applied to the Gradle subprocess. Gradle runs with full user
  privileges.
- The tool does not perform cryptographic operations and has no authentication
  or authorization mechanisms.
