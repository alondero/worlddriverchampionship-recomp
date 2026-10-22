# Release notes template

Copy the body below to `drafts/<version>-release-notes.md`, relative to this
skill. Replace every token, remove inapplicable sections, and resolve all
links. This is a preparation template, not evidence that packages exist.

```markdown
> **Pre-release quality.** An experimental World Driver Championship port
> for early testing and feedback. <QUALITY_AND_PLATFORM_LIMITS>

## You must supply your own ROM

This download contains no ROM dump. Supply your own big-endian 16 MiB USA
dump, named `World Driver Championship (USA).z64`. See [Building][building]
for the required ROM identity and source-build instructions.

## What's working

<MAIN_PLAYER_VISIBLE_CHANGE_AND_ACTUAL_GAMEPLAY_EVIDENCE>

## Closed since <PREV_VERSION>

<GROUPED_CHANGES_WITH_ISSUE_AND_PR_LINKS_OR_OMIT_FOR_FIRST_RELEASE>

## What's not done yet

<CURRENT_PLAYER_LIMITATIONS_AND_FOLLOW_UP_LINKS>

Report problems with the release version, operating system, GPU and driver,
output text, and the steps and in-game location needed to reproduce them.

## How to run

<ONLY_THE_PACKAGES_ACTUALLY_PUBLISHED_AND_VERIFIED_LAUNCH_INSTRUCTIONS>

<WORKING_DIRECTORY_ROM_ARGUMENT_RUNTIME_FILES_AND_SAVE_LOCATION>

See [README][readme] for controls and [Configuration][configuration] for
graphics settings and save behavior. State which platforms were built and
which were tested in-game.

## Build provenance

- Commit: `<FULL_SHA>` (<COMMIT_DATE>)
- Workflow: [<RUN_ID>][run]
- Packages: <ASSET_FILENAMES>
- Toolchains and runtime requirements: <MEASURED_BUILD_DETAILS>
- Validation: <CHECKS_ACTUALLY_PERFORMED_AND_SKIPPED>

---

For source builds, see [Building][building].

[building]: https://github.com/alondero/worlddriverchampionship-recomp/blob/<VERSION>/BUILDING.md
[readme]: https://github.com/alondero/worlddriverchampionship-recomp/blob/<VERSION>/README.md
[configuration]: https://github.com/alondero/worlddriverchampionship-recomp/blob/<VERSION>/docs/configuration.md
[run]: https://github.com/alondero/worlddriverchampionship-recomp/actions/runs/<RUN_ID>
```
