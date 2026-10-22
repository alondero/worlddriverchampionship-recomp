---
name: release
description: Prepare or publish a World Driver Championship Recompiled release, including change summaries, tag and workflow checks, player-facing notes, and asset verification. Use for release preparation or an explicit request to cut a version.
---

# Release

Prepare **World Driver Championship Recompiled** releases for
`alondero/worlddriverchampionship-recomp`. Follow the sibling projects' five
phases: gather changes, tag and dispatch, wait, verify, replace the notes.
Run commands from the repository root. Helpers require Python 3.11+, Git,
and authenticated GitHub CLI; they work directly in PowerShell or a POSIX shell.

## Inputs and scope

- Version: `v<MAJOR>.<MINOR>.<PATCH>`, such as `v0.1.0`.
- Base: the previous published release tag reachable from the target commit.
  For the first release, omit the base to include the entire history.
- Target: the exact reviewed commit on `main`, including a squash merge.
  Never substitute an older merge commit for the requested source state.

Preparing this skill or drafting notes does not authorize publishing a release.
For a request to publish, complete the reviewable notes and readiness checks
before pushing the tag or dispatching CI. Follow existing authorization for
private ROM inputs and CI use; ask only if that authorization is missing.

## Readiness gate

Read [the workflow contract and gotchas](references/gotchas.md) before tagging.
Inspect `.github/workflows/build-release.yml` at the target commit and compare
it with the registered workflow:

```text
gh workflow view build-release.yml --repo alondero/worlddriverchampionship-recomp --ref main --yaml
```

**The current workflow is incomplete:** it uploads an Actions artifact but
has no job creating a GitHub release. Preparation can continue; tagging and dispatching a release must
wait for a reviewed publishing pipeline. Report this gap with the prepared
notes. Implementing that pipeline is separate work unless the user requests it.

After the pipeline changes, recheck its actual inputs, jobs, checkout ref,
packaging, ROM cleanup, and published filenames. Confirm `ROM_ASSETS_REPO`
and `ROM_ASSETS_PAT` are configured without printing secret values. Check
the package includes the runtime dependencies and frontend assets required
by the target build. A successful upload alone is insufficient.

Do not inherit sibling platform claims: Windows has the documented gameplay
evidence; Linux and Android remain unvalidated in the current documentation.
Read `README.md`, `BUILDING.md`, `docs/first-playable.md`, and
`docs/reference/rom.md` at the target before describing support.

## 1. Gather changes and draft the notes

Resolve the intended target and check whether a previous release exists:

```text
git fetch origin main --tags
gh release list --repo alondero/worlddriverchampionship-recomp --json tagName,isDraft,isPrerelease,publishedAt
python .claude/skills/release/scripts/release.py changes --head origin/main
```

For a subsequent release, add `--base <previous-release-tag>`. The helper
uses the exact Git range to select merged PRs. Closed issues are labelled
candidates: confirm their fixes are present before listing them as shipped.
Read the relevant PR bodies with `gh pr view <number> --repo
alondero/worlddriverchampionship-recomp`.

Draft `.claude/skills/release/drafts/<version>-release-notes.md` from
[the notes template](references/release-notes-template.md). Drafts are ignored.
Use [the first and subsequent release guidance](references/prior-release-format.md)
for the change section. Finish the notes from measured evidence; distinguish
build success from gameplay validation.

## 2. Tag and dispatch (after the readiness gate passes)

Require a clean tracked working tree on `main`, with `HEAD` equal to freshly
fetched `origin/main`. Record its full SHA. Inspect recursive submodule pins;
expected dependency patch modifications are documented in `patches/README.md`.
Release content comes from committed sources and pins, not local build output.

Check both local and remote tags before creating anything:

```text
git status --short
git submodule status --recursive
git rev-parse HEAD
git rev-parse origin/main
git tag --list <version>
git ls-remote --tags origin refs/tags/<version> refs/tags/<version>^{}
```

For a new tag, tag the recorded SHA and push that exact ref:

```text
git tag <version> <full-target-sha>
git push origin refs/tags/<version>
```

If the tag already exists, compare its peeled commit with the recorded SHA.
Reuse it only when identical. Preserve existing tags on mismatch.

Dispatch `build-release.yml` against `--ref <version>` with `tag=<version>`
and only the publishing inputs established by the readiness check. The
current input schema is recorded in the workflow contract reference.
Do not dispatch the current artifact-only workflow as a release.
For the sibling convention, the completed publisher must produce
`draft=false, prerelease=false`; the body may still say "Pre-release quality".

Capture existing run IDs before dispatch. Identify the new run by workflow,
`workflow_dispatch` event, tag branch, exact SHA, and dispatch time:

```text
gh run list --repo alondero/worlddriverchampionship-recomp --workflow build-release.yml --event workflow_dispatch --branch <version> --commit <full-target-sha> --json databaseId,createdAt,headSha,headBranch,status,url
```

If no unique new run can be identified, report the ambiguity and inspect it;
avoid redispatching blindly. A failed dispatch can leave a pushed tag behind.
Resume from that tag after fixing the cause rather than moving or deleting it.

## 3. Wait for the identified build

```text
gh run view <run-id> --repo alondero/worlddriverchampionship-recomp --json status,conclusion,headSha,headBranch,jobs,url
```

Poll in bounded intervals and keep the user informed. A failed or cancelled
run stops publication follow-up. Require the expected build and publishing
jobs to succeed; inspect any skipped job before treating the run as complete.

## 4. Verify the published release

```text
python .claude/skills/release/scripts/release.py verify <version> <run-id>
```

The helper checks the origin tag against the local commit, workflow identity,
run SHA/tag/conclusion, tag-keyed release URL, flags, and nonempty assets.
Its default expected asset is the current workflow's sole package,
`worlddriverchampionship-recomp-linux-x64.zip`. After a packaging change,
pass one `--asset <exact-filename>` for **each** required release asset;
explicit assets replace that default. Verify the list against the target
workflow, never against whichever files happened to upload.

Download the actual release assets to an ignored directory, inspect archive
integrity and contents, and check required runtime files. Exclude ROM dumps,
private checkout contents, saves, and diagnostic captures. Record any platform
smoke test performed and the limits of that evidence.

## 5. Replace the notes and verify the final result

```text
python .claude/skills/release/scripts/release.py check-notes .claude/skills/release/drafts/<version>-release-notes.md
gh release edit <version> --repo alondero/worlddriverchampionship-recomp --title "World Driver Championship Recompiled <version>" --notes-file .claude/skills/release/drafts/<version>-release-notes.md
```

Use a UTF-8 notes file; passing multiline prose inline through Windows shells
can corrupt punctuation. Re-run `verify` with `--notes-file <path>` and the
same required asset list to compare the published title and body exactly.
Report the release URL, full source SHA, workflow URL, verified assets,
tests actually performed, and remaining limitations. If preparation stopped
at readiness, report the draft path and concrete pipeline gaps instead.
