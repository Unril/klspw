#!/usr/bin/env bash
# Resolve GitHub Actions tags to full commit SHAs for pinning in workflows.
# Also checks vcpkg version pinning across workflows and Dockerfiles.
#
# Usage:
#   ./scripts/resolve-action-shas.sh              # all actions + vcpkg, latest tag each
#   ./scripts/resolve-action-shas.sh -n 3          # latest 3 tags each
#   ./scripts/resolve-action-shas.sh actions/cache  # single action
#   ./scripts/resolve-action-shas.sh actions/cache 3

set -euo pipefail

resolve_sha() {
  local repo=$1 tag=$2
  local sha deref
  sha=$(git ls-remote "https://github.com/${repo}.git" "refs/tags/${tag}" | head -1 | cut -f1)
  deref=$(git ls-remote "https://github.com/${repo}.git" "refs/tags/${tag}^{}" | head -1 | cut -f1)
  echo "${deref:-$sha}"
}

latest_tags() {
  local repo=$1 count=$2 pattern=${3:-'refs/tags/v*'}
  git ls-remote --tags "https://github.com/${repo}.git" "$pattern" 2>/dev/null |
    grep -v '\^{}' |
    sed 's|.*refs/tags/||' |
    sort -rV |
    head -"$count"
}

print_action() {
  local repo=$1 count=$2
  echo "--- ${repo} ---"
  local tags
  tags=$(latest_tags "$repo" "$count")
  if [[ -z "$tags" ]]; then
    echo "  (no tags found)"
    return
  fi
  while IFS= read -r tag; do
    local sha
    sha=$(resolve_sha "$repo" "$tag")
    printf "  uses: %-50s # %s\n" "${repo}@${sha}" "$tag"
  done <<<"$tags"
  echo
}

# Scan workflows for pinned actions and their current versions.
scan_workflows() {
  echo "=== Currently pinned in .github/workflows/ ==="
  echo
  rg --no-heading --no-filename -oN 'uses: ([^@]+)@\S+ # (.+)' .github/workflows/ -r '$1  $2' |
    sort -u |
    while IFS= read -r line; do
      local action version
      action=$(echo "$line" | awk '{print $1}')
      version=$(echo "$line" | awk '{print $2}')
      printf "  %-50s %s\n" "$action" "$version"
    done
  echo
}

# Discover unique action repos from workflows.
discover_actions() {
  rg --no-heading --no-filename -oN 'uses: ([^@]+)@' .github/workflows/ -r '$1' |
    sed 's|/init$||; s|/analyze$||; s|/upload-sarif$||; s|/actions/[^/]*$||' |
    sort -u
}

# Check vcpkg version pinning across workflows and Dockerfiles.
check_vcpkg() {
  echo "=== vcpkg version pinning ==="
  echo

  local pinned
  pinned=$(rg --no-heading --no-filename -oN '\-\-branch ([0-9.]+)' .github/workflows/ .clusterfuzzlite/ -r '$1' 2>/dev/null |
    sort -u)

  if [[ -z "$pinned" ]]; then
    echo "  WARNING: vcpkg is not pinned to a release tag"
  else
    local count
    count=$(echo "$pinned" | wc -l | tr -d ' ')
    if [[ "$count" -gt 1 ]]; then
      echo "  WARNING: inconsistent vcpkg versions:"
      echo "$pinned" | while IFS= read -r v; do echo "    $v"; done
    else
      printf "  pinned: %s\n" "$pinned"
    fi
  fi

  echo
  echo "  latest:"
  local latest
  latest=$(latest_tags "microsoft/vcpkg" 3 'refs/tags/2*')
  echo "$latest" | while IFS= read -r tag; do
    printf "    %s\n" "$tag"
  done
  echo
}

count=1

if [[ $# -ge 1 && "$1" == "-n" ]]; then
  count=${2:-1}
  shift 2 || true
elif [[ $# -ge 1 && "$1" != -* ]]; then
  repo=$1
  count=${2:-1}
  print_action "$repo" "$count"
  exit 0
fi

scan_workflows
check_vcpkg

echo "=== Latest available ==="
echo

for repo in $(discover_actions); do
  print_action "$repo" "$count"
done
