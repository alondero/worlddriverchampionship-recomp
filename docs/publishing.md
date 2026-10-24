# Publishing

Publish hand-written source, measured configuration, dependency patches and
licensed frontend assets. Keep the original game dump, translated game code,
captures, saves, credentials and local agent state outside the tracked tree.
The Gradle wrapper and licensed frontend fonts are intentional binary inputs.

## Source checks

Run the source checks and [ROM-free host tests](testing.md#rom-free-host-tests).
The source workflow runs on every push and pull request with read-only repository
permissions and no private ROM access. It checks ignored-but-tracked files,
documentation, Python tests, C++ host tests, workflow syntax and credentials.

```bash
python -B scripts/check_docs.py
python -B scripts/check_repo.py
python -B -m unittest discover tests
git diff --check
```

Ignore rules do not remove files already committed. The repository check rejects
ignored files even when they were added with `git add -f`. Keep reusable source
and documentation images under `assets/` or `docs/`; root-level captures are
ignored. Shared agent guidance and the generated-file hook are intentional;
machine-specific settings and worktree directories are ignored.

Before the first public publication, scan all available history with
[Gitleaks](https://github.com/gitleaks/gitleaks), including remote branches and
tags, and inspect historical filenames for ROMs or generated game output:

```bash
gitleaks git --redact --log-opts="--all" --no-banner .
git rev-list --objects --all
```

If sensitive content is found in history, removing the current file is
insufficient. Resolve that history and any exposed credentials before publishing.

## GitHub and distribution

Review releases, workflow artifacts and logs as well as the source tree.
Use the source workflow's `check`, `host-tests` and `secrets` jobs as required
pull-request checks when configuring branch protection. Enable GitHub secret
scanning and push protection when available for the repository.

The manual build workflow requires a separately configured private ROM source:
the `ROM_ASSETS_REPO` repository variable and the `ROM_ASSETS_PAT` secret. It
builds the Windows x64 and Linux x64 players, runs the test suite, removes the
ROM, and packages the player, its runtime libraries and the frontend assets. A
publishing run creates the GitHub Release for the dispatched tag and attaches
both packages; a `publish: false` run only builds, so the pipeline can be
validated before a tag exists. Review package contents and retain project,
dependency and font notices before distributing a build. The public source
checks do not validate a playable build; Windows is the tested player platform,
while Linux and Android player support remain unvalidated.
