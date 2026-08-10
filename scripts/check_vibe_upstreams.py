#!/usr/bin/env python3
"""Validate pinned Vibe dependencies and optionally check their remote heads."""

from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
LOCK_PATH = ROOT / "integrations" / "vibe" / "upstreams.json"


def git(*args: str, cwd: Path = ROOT) -> str:
    return subprocess.run(
        ["git", *args],
        cwd=cwd,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()


def remote_head(url: str, branch: str) -> str:
    output = git("ls-remote", url, f"refs/heads/{branch}")
    if not output:
        raise RuntimeError(f"remote branch not found: {url}#{branch}")
    return output.split()[0]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--remote",
        action="store_true",
        help="also compare each pin with the configured remote branch head",
    )
    args = parser.parse_args()

    dependencies = json.loads(LOCK_PATH.read_text(encoding="utf-8"))["dependencies"]
    failures: list[str] = []
    updates: list[str] = []

    print("# Vibe dependency status")
    print()
    for name, dependency in dependencies.items():
        path = ROOT / dependency["path"]
        expected = dependency["commit"]
        if not (path / ".git").exists():
            failures.append(
                f"{name}: submodule missing; run `git submodule update --init --recursive`"
            )
            continue

        actual = git("rev-parse", "HEAD", cwd=path)
        dirty = git("status", "--porcelain", cwd=path)
        staged_line = git("ls-files", "--stage", dependency["path"])
        staged = staged_line.split()[1] if staged_line else "missing"
        status = "OK"
        if actual != expected:
            status = "ERROR"
            failures.append(f"{name}: checkout {actual} does not match lock {expected}")
        if staged != expected:
            status = "ERROR"
            failures.append(f"{name}: gitlink {staged} does not match lock {expected}")
        if dirty:
            status = "ERROR"
            failures.append(f"{name}: submodule contains local modifications")

        remote = "not checked"
        if args.remote:
            remote = remote_head(dependency["url"], dependency["branch"])
            if remote != expected:
                status = "UPDATE AVAILABLE" if status == "OK" else status
                updates.append(
                    f"{name}: {expected[:12]} -> {remote[:12]} "
                    f"({dependency['url']})"
                )

        print(
            f"- **{name}**: {status}; release `{dependency['release']}`; "
            f"pinned `{expected}`; remote `{remote}`"
        )

    if failures:
        print("\n## Errors")
        for failure in failures:
            print(f"- {failure}")
    if updates:
        print("\n## Updates available")
        for update in updates:
            print(f"- {update}")

    if failures:
        return 1
    if updates:
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
