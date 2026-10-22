# First and subsequent releases

Match the sibling projects' useful structure, using WDC evidence and names:
quality statement, own-ROM requirement, what's working, changes, current
limitations, how to run, and build provenance.

For a first release, describe the tested player path and omit "Closed since".
The `changes` helper without a base includes the root commit; using
`<root-sha>..<target>` would exclude it. Confirm there is no earlier published
release before claiming this is the first one.

For later releases, choose the previous published release tag reachable from
the target, not an arbitrary recent tag. Group related changes by player
benefit, with links to the issues and PRs actually represented in the range.
A closed issue may describe work outside the release; check its resolution.

Keep the notes concise. Use categories only when they help readers, refresh
known limitations, and include developer-only changes when they affect
building, diagnosing, or using this release. Link repository documents at the
release tag, and record the actual workflow/toolchains rather than copying
the sibling releases' versions or performance claims.
