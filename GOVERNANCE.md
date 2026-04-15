# Governance

## Overview

klspw is a single-maintainer open source project. Decisions about direction,
releases, and contributions are made by the maintainer.

## Roles

- Maintainer: [Nikolai Fedorov](https://github.com/Unril) -- owns the repository,
  reviews and merges contributions, manages releases, and responds to security
  reports.
- Contributors: anyone who submits issues, pull requests, or participates in
  discussions. Contributions are welcome from everyone who follows the
  [Code of Conduct](CODE_OF_CONDUCT.md).

## Decision process

1. Changes go through pull requests against the `master` branch.
2. CI must pass before merging (build, test, static analysis, fuzzing on PRs).
3. The maintainer reviews and merges PRs.
4. For significant changes (new features, breaking changes, architecture shifts),
   the maintainer may request discussion in an issue before accepting a PR.

## Releases

The maintainer decides when to cut releases. The release process is documented
in [CONTRIBUTING.md](CONTRIBUTING.md#publishing-a-release).

## Amendments

This governance model may evolve as the project grows. Changes to governance
follow the same PR process as code changes.
