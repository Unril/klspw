#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Nikolai Fedorov
#
# SPDX-License-Identifier: MIT

# Resolve GitHub Actions tags to full commit SHAs, check vcpkg and Docker pinning.
# Delegates to Python via uv for the actual logic.
#
# Usage:
#   ./scripts/resolve-action-shas.sh              # full report
#   ./scripts/resolve-action-shas.sh -n 3          # latest 3 tags each
#   ./scripts/resolve-action-shas.sh actions/cache  # single action

set -euo pipefail
exec uv run --quiet --script "$(dirname "$0")/resolve-action-shas-impl.py" "$@"
