#!/usr/bin/env python3
"""Read-only WDC release preparation and provenance checks (Python 3.11+)."""
from __future__ import annotations

import argparse
from datetime import datetime
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[4]
REPO = "alondero/worlddriverchampionship-recomp"
WORKFLOW = ".github/workflows/build-release.yml"
DEFAULT_ASSETS = ["worlddriverchampionship-recomp-linux-x64.zip"]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def command(*args: str) -> str:
    result = subprocess.run(args, cwd=ROOT, text=True, encoding="utf-8",
                            capture_output=True, check=True)
    return result.stdout.strip()


def api(path: str):
    return json.loads(command("gh", "api", f"repos/{REPO}/{path}"))


def pages(path: str):
    # --slurp keeps page boundaries valid JSON; no soft result-count limit.
    result = command("gh", "api", "--paginate", "--slurp", f"repos/{REPO}/{path}")
    return [item for page in json.loads(result) for item in page]


def commit(ref: str) -> str:
    return command("git", "rev-parse", "--verify", "--end-of-options", f"{ref}^{{commit}}")


def version_arg(value: str) -> str:
    if not re.fullmatch(r"v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)", value):
        raise argparse.ArgumentTypeError("expected v<MAJOR>.<MINOR>.<PATCH>")
    return value


def run_arg(value: str) -> str:
    if not re.fullmatch(r"[1-9][0-9]*", value):
        raise argparse.ArgumentTypeError("run ID must be a positive integer")
    return value


def timestamp(value: str) -> datetime:
    return datetime.fromisoformat(value.replace("Z", "+00:00"))


def changes(base: str | None, head: str) -> None:
    target = commit(head)
    base_sha = commit(base) if base else None
    if base_sha:
        command("git", "merge-base", "--is-ancestor", base_sha, target)
    revision_range = f"{base_sha}..{target}" if base_sha else target
    included = set(command("git", "rev-list", revision_range).splitlines())
    lower = timestamp(command("git", "show", "-s", "--format=%cI", base_sha)) if base_sha else None
    prs = pages("pulls?state=closed&base=main&per_page=100")
    issues = pages("issues?state=closed&per_page=100")

    print(f"## Changes: {base or 'entire history'} -> {target}\n")
    print("### Commits\n")
    print(command("git", "log", "--oneline", revision_range) or "(none)")
    print("\n### Merged PRs represented in this exact Git range\n")
    for pr in sorted(prs, key=lambda item: item["number"]):
        if pr.get("merged_at") and pr.get("merge_commit_sha") in included:
            print(f"- [PR #{pr['number']}]({pr['html_url']}): {pr['title']}")
    print("\n### Closed issue candidates (check each fix is included)\n")
    for issue in sorted(issues, key=lambda item: item["number"]):
        closed = issue.get("closed_at")
        if "pull_request" in issue or not closed:
            continue
        when = timestamp(closed)
        if lower is None or when > lower:
            print(f"- [#{issue['number']}]({issue['html_url']}): {issue['title']}")
    print("\nIssue closure dates are discovery hints, not proof of shipped fixes.")
    print("PR matching uses merge-commit ancestry; manually account for rebased PRs.")
    print(f"Tag target: {target} (confirm this is the reviewed main tip).")


def check_notes(path: Path) -> str:
    body = path.read_text(encoding="utf-8")
    require(bool(body.strip()), "notes are empty")
    require(not body.startswith("\ufeff"), "save notes as UTF-8 without BOM")
    require("Automated build from commit" not in body, "notes contain the automated placeholder")
    require(not re.search(r"<(?:[A-Z][A-Z0-9_ -]*|version|tag|run-id)>", body),
            "notes contain unfinished template tokens")
    definitions = set(re.findall(r"^ {0,3}\[([^]\n]+)\]:", body, flags=re.MULTILINE))
    references = [label or text for text, label in
                  re.findall(r"\[([^]\n]+)\]\[([^]\n]*)\]", body)]
    # Markdown reference labels are case-insensitive, with normalized whitespace.
    normalize = lambda label: " ".join(label.split()).casefold()
    defined = {normalize(label) for label in definitions}
    missing = {label for label in references if normalize(label) not in defined}
    require(not missing, f"undefined reference links: {', '.join(sorted(missing))}")
    return body


def verify(version: str, run_id: str, assets: list[str], notes: Path | None) -> None:
    target = commit(f"refs/tags/{version}")
    origin = command("git", "remote", "get-url", "origin")
    normalized_origin = origin.removesuffix(".git").rstrip("/")
    require(normalized_origin in {f"https://github.com/{REPO}", f"git@github.com:{REPO}",
                                  f"ssh://git@github.com/{REPO}"},
            "origin must point to the WDC release repository")
    remote = command("git", "ls-remote", "--tags", "origin",
                     f"refs/tags/{version}", f"refs/tags/{version}^{{}}")
    refs = {line.split()[1]: line.split()[0] for line in remote.splitlines()}
    remote_sha = refs.get(f"refs/tags/{version}^{{}}", refs.get(f"refs/tags/{version}"))
    require(remote_sha == target, "origin tag is missing or differs from the local tag commit")

    run = api(f"actions/runs/{run_id}")
    require(run["path"].split("@", 1)[0] == WORKFLOW, "run belongs to another workflow")
    require(run["event"] == "workflow_dispatch", "run was not manually dispatched")
    require(run["head_sha"] == target, "workflow run SHA differs from the release tag")
    require(run["head_branch"] == version, "workflow run was not dispatched against the tag")
    require(run["status"] == "completed" and run["conclusion"] == "success",
            "workflow has not completed successfully")

    release = api(f"releases/tags/{version}")
    expected_url = f"https://github.com/{REPO}/releases/tag/{version}"
    require(release["tag_name"] == version, "release tag_name differs from the requested version")
    require(release["html_url"] == expected_url, "release URL is not bound to the requested tag")
    require(release["draft"] is False, "release is still a draft")
    require(release["prerelease"] is False, "release is marked as a prerelease")
    # The release response can truncate the embedded asset list. Paginate it.
    uploaded = pages(f"releases/{release['id']}/assets?per_page=100")
    for name in assets:
        matching = [asset for asset in uploaded if asset["name"] == name]
        require(len(matching) == 1 and matching[0]["size"] > 0
                and matching[0].get("state") == "uploaded",
                f"asset missing, empty, duplicated, or not uploaded: {name}")
    if notes:
        body = check_notes(notes)
        require(release["name"] == f"World Driver Championship Recompiled {version}",
                "published release title differs from the expected title")
        require((release.get("body") or "").replace("\r\n", "\n").rstrip()
                == body.replace("\r\n", "\n").rstrip(),
                "published notes differ from the reviewed notes file")
    print(f"Verified release metadata: {expected_url}")
    print(f"Commit: {target}\nWorkflow: {run['html_url']}")
    print(f"Required assets: {', '.join(assets)}")
    print("Archive contents, runtime dependencies, and gameplay require separate checks.")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="action", required=True)
    summary = commands.add_parser("changes", help="list exact-range PRs and issue candidates")
    summary.add_argument("--base", help="omit for the first release, including its root commit")
    summary.add_argument("--head", default="origin/main")
    checker = commands.add_parser("check-notes", help="check a local UTF-8 notes draft")
    checker.add_argument("notes_file", type=Path)
    verifier = commands.add_parser("verify", help="verify an already published release")
    verifier.add_argument("version", type=version_arg)
    verifier.add_argument("run_id", type=run_arg)
    verifier.add_argument("--asset", action="append", help="required asset; repeat for every filename")
    verifier.add_argument("--notes-file", type=Path)
    args = parser.parse_args()
    try:
        if args.action == "changes":
            changes(args.base, args.head)
        elif args.action == "check-notes":
            check_notes(args.notes_file)
            print("Notes checks passed; review factual claims and links before publishing.")
        else:
            verify(args.version, args.run_id, args.asset or DEFAULT_ASSETS, args.notes_file)
    except (ValueError, KeyError, OSError, subprocess.CalledProcessError) as error:
        detail = error.stderr.strip() if isinstance(error, subprocess.CalledProcessError) else str(error)
        print(f"error: {detail or error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
