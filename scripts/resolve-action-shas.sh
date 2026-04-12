#!/usr/bin/env bash
# Resolve GitHub Actions tags to full commit SHAs for pinning in workflows.
# Handles both lightweight and annotated tags.
#
# Usage:
#   ./scripts/resolve-action-shas.sh              # all actions from workflows, latest tag + current pinned
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
  local repo=$1 count=$2
  git ls-remote --tags "https://github.com/${repo}.git" 'refs/tags/v*' 2>/dev/null |
    grep -v '\^{}' |
    sed 's|.*refs/tags/||' |
    grep -E '^v[0-9]+(\.[0-9]+)*$' |
    sort -rV |
    head -"$count"
}

print_action() {
  local repo=$1 count=$2
  echo "--- ${repo} ---"
  local tags
  tags=$(latest_tags "$repo" "$count")
  if [[ -z "$tags" ]]; then
    echo "  (no semver tags found)"
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
    sed 's|/init$||; s|/analyze$||; s|/upload-sarif$||' |
    sort -u
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

echo "=== Latest available ==="
echo

for repo in $(discover_actions); do
  print_action "$repo" "$count"
done
