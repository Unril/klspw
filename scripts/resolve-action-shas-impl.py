# /// script
# requires-python = ">=3.13"
# ///
"""Resolve GitHub Actions tags to commit SHAs and check dependency pinning.

Reports:
  - Currently pinned actions in .github/workflows/
  - vcpkg version consistency (vcpkg-configuration.json baseline vs workflow clones)
  - Docker image digest pinning in Dockerfiles
  - Latest available tags for each action
"""

import argparse
import json
import re
import subprocess
from pathlib import Path
from typing import cast

ROOT = Path(__file__).resolve().parent.parent


# --- git helpers ---


def git_ls_remote(repo: str, pattern: str) -> list[tuple[str, str]]:
    """Return [(sha, ref), ...] from git ls-remote."""
    url = f"https://github.com/{repo}.git"
    result = subprocess.run(
        ["git", "ls-remote", "--tags", url, pattern],
        capture_output=True,
        text=True,
        timeout=30,
        check=False,
    )
    if result.returncode != 0:
        return []
    pairs: list[tuple[str, str]] = []
    for line in result.stdout.strip().splitlines():
        sha, ref = line.split("\t", 1)
        pairs.append((sha, ref))
    return pairs


def resolve_sha(repo: str, tag: str) -> str:
    """Resolve a tag to its commit SHA (handles annotated tags)."""
    url = f"https://github.com/{repo}.git"
    result = subprocess.run(
        ["git", "ls-remote", url, f"refs/tags/{tag}", f"refs/tags/{tag}^{{}}"],
        capture_output=True,
        text=True,
        timeout=30,
        check=False,
    )
    lines = result.stdout.strip().splitlines()
    if not lines:
        return ""
    # Prefer dereferenced (^{}) entry for annotated tags.
    for line in reversed(lines):
        sha = line.split("\t", 1)[0]
        if sha:
            return sha
    return ""


def _parse_version(tag: str) -> list[int]:
    """Extract numeric components from a version tag for sorting."""
    return [int(x) for x in cast(list[str], re.findall(r"\d+", tag))]


def latest_tags(repo: str, count: int, pattern: str = "refs/tags/v*") -> list[str]:
    """Return the latest N semver tags for a repo, sorted descending."""
    pairs = git_ls_remote(repo, pattern)
    tags: list[str] = []
    for _, ref in pairs:
        if ref.endswith("^{}"):
            continue
        tag = ref.removeprefix("refs/tags/")
        # Accept vN, vN.N, vN.N.N, and YYYY.MM.DD.
        if re.match(r"^v?\d+(\.\d+)*$", tag):
            tags.append(tag)
    tags.sort(key=_parse_version, reverse=True)
    return tags[:count]


# --- scanning ---


def scan_pinned_actions() -> list[tuple[str, str]]:
    """Scan workflows for pinned actions and their version comments."""
    pattern = re.compile(r"uses:\s+([^@]+)@\S+\s+#\s+(.+)")
    results: set[tuple[str, str]] = set()
    workflows = ROOT / ".github" / "workflows"
    if not workflows.is_dir():
        return []
    for f in sorted(workflows.glob("*.yml")):
        for line in f.read_text().splitlines():
            m = pattern.search(line)
            if m:
                results.add((m.group(1).strip(), m.group(2).strip()))
    return sorted(results)


def discover_action_repos() -> list[str]:
    """Discover unique action repos from workflow files."""
    pattern = re.compile(r"uses:\s+([^@]+)@")
    repos: set[str] = set()
    workflows = ROOT / ".github" / "workflows"
    if not workflows.is_dir():
        return []
    for f in sorted(workflows.glob("*.yml")):
        for line in f.read_text().splitlines():
            m = pattern.search(line)
            if m:
                action = m.group(1).strip()
                # Normalize sub-actions to repo level.
                parts = action.split("/")
                if (len(parts) > 2 and parts[0] != "google") or len(parts) > 3:
                    action = "/".join(parts[:2])
                repos.add(action)
    return sorted(repos)


# --- vcpkg checks ---


def check_vcpkg() -> None:
    """Check vcpkg version consistency between config and workflows."""
    print("=== vcpkg version pinning ===")
    print()

    # Read baseline from vcpkg-configuration.json.
    config_file = ROOT / "vcpkg-configuration.json"
    baseline: str = ""
    if config_file.exists():
        try:
            data = cast(dict[str, object], json.loads(config_file.read_text()))
            registry = data.get("default-registry")
            if isinstance(registry, dict):
                reg = cast(dict[str, str], registry)
                bl = reg.get("baseline")
                if isinstance(bl, str):
                    baseline = bl
        except (json.JSONDecodeError, KeyError):
            pass

    if baseline:
        print(f"  vcpkg-configuration.json baseline: {baseline[:12]}...")
    else:
        print("  WARNING: no baseline in vcpkg-configuration.json")

    # Find commit refs in workflow clone/checkout commands.
    workflow_commits: set[str] = set()
    branch_pattern = re.compile(r"--branch\s+(\S+)")

    search_dirs = [ROOT / ".github" / "workflows", ROOT / ".clusterfuzzlite"]
    for d in search_dirs:
        if not d.is_dir():
            continue
        for f in d.iterdir():
            if f.suffix not in (".yml", ".yaml") and f.name != "Dockerfile":
                continue
            content = f.read_text()
            # Scan for checkout on lines following vcpkg clone.
            for m in re.finditer(r"checkout\s+([0-9a-f]{40})", content):
                start = max(0, m.start() - 200)
                if "vcpkg" in content[start : m.start()]:
                    workflow_commits.add(m.group(1))
            # Also check --branch tags.
            for line in content.splitlines():
                if "microsoft/vcpkg" not in line:
                    continue
                m2 = branch_pattern.search(line)
                if m2:
                    workflow_commits.add(f"tag:{m2.group(1)}")

    if workflow_commits:
        print(f"  workflow clones: {', '.join(sorted(workflow_commits))}")
    else:
        print("  WARNING: no vcpkg clone found in workflows")

    # Consistency check.
    if baseline and workflow_commits:
        short_baseline: str = baseline[:12]
        mismatches = [
            c
            for c in workflow_commits
            if not c.startswith(short_baseline) and c != f"tag:{baseline}"
        ]
        if mismatches:
            print(f"  WARNING: workflow commits don't match baseline {short_baseline}")
        else:
            print("  consistent")

    # Latest available.
    print()
    print("  latest tags:")
    for tag in latest_tags("microsoft/vcpkg", 3, "refs/tags/2*"):
        print(f"    {tag}")
    print()


# --- Docker checks ---


def check_docker_pins() -> None:
    """Check Docker image pinning in Dockerfiles."""
    print("=== Docker image pinning ===")
    print()

    dockerfiles = list(ROOT.rglob("Dockerfile"))
    dockerfiles = [
        d for d in dockerfiles if "build" not in str(d) and "vcpkg" not in str(d)
    ]

    if not dockerfiles:
        print("  (no Dockerfiles found)")
        print()
        return

    for df in sorted(dockerfiles):
        rel = str(df.relative_to(ROOT))
        for line in df.read_text().splitlines():
            if not line.strip().startswith("FROM "):
                continue
            if "@sha256:" in line:
                print(f"  {rel:<50} pinned")
            else:
                image = line.split()[1]
                print(f"  {rel:<50} WARNING: {image} not pinned by digest")

    print()


# --- main ---


def print_action(repo: str, count: int) -> None:
    """Print latest tags and their SHAs for an action repo."""
    print(f"--- {repo} ---")
    tags = latest_tags(repo, count)
    if not tags:
        print("  (no tags found)")
        return
    for tag in tags:
        sha = resolve_sha(repo, tag)
        print(f"  uses: {repo}@{sha:<40} # {tag}")
    print()


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Resolve GitHub Actions SHAs and check pinning."
    )
    _ = parser.add_argument(
        "-n", type=int, default=1, help="Number of latest tags to show (default: 1)"
    )
    _ = parser.add_argument("repo", nargs="?", help="Single action repo to resolve")
    _ = parser.add_argument(
        "count", nargs="?", type=int, help="Override -n for single repo"
    )
    args = parser.parse_args()

    count_arg: int | None = cast(int | None, args.count)
    n_arg: int = cast(int, args.n)
    count: int = count_arg if count_arg is not None else n_arg
    repo: str | None = cast(str | None, args.repo)

    if repo is not None:
        print_action(repo, count)
        return

    # Full report.
    pinned = scan_pinned_actions()
    if pinned:
        print("=== Currently pinned in .github/workflows/ ===")
        print()
        for action, version in pinned:
            print(f"  {action:<50} {version}")
        print()

    check_vcpkg()
    check_docker_pins()

    print("=== Latest available ===")
    print()
    for repo in discover_action_repos():
        print_action(repo, count)


if __name__ == "__main__":
    main()
