# Workflow contract and release gotchas

## WDC's current workflow is an artifact build

`.github/workflows/build-release.yml` is named **Build and release**, with
dispatch inputs `tag` (required) and `draft` (default true). The only job is
`build-linux`; it checks out `inputs.tag`, fetches the private ROM, runs
`build.sh`, removes the private inputs, and uploads:

- Actions artifact: `worlddriverchampionship-linux-x64`
- File: `worlddriverchampionship-recomp-linux-x64.zip`

There is no Windows or Android build job and no GitHub release publishing
job. `contents: write`, the workflow name, and an unused `draft` input do not
create a release. A green run is currently evidence of an artifact build.
Use the workflow filename in CLI calls; the siblings' `Build & Release`
display name does not match this repository.

The Linux package currently copies the executable and optionally `assets/`.
Before publication, review its shared-library requirements, required frontend
assets, launch instructions, and clean-machine behavior. Windows packaging
would also need the DLLs described in `BUILDING.md`. Android is scaffold-only.
Derive any future asset list and toolchain claims from the reviewed workflow.

## Keep source, tag, and build aligned

WDC uses squash merges as well as ordinary commits. Release the reviewed
`main` tip; requiring a second parent or tagging the most recent merge can
silently omit shipped fixes. Dispatch against the immutable tag, with the
checkout also pinned to that tag. Match workflow runs by tag and full SHA;
the latest queued run might belong to another release or contributor.

An existing tag is a resume point only if its commit matches the target.
Publication failures do not authorize deleting or moving a tag. Check for an
existing run or release before retrying an external action.

## Draft state and quality wording are separate

The sibling convention publishes with GitHub `draft=false` and
`prerelease=false`, while describing the build's experimental quality in the
notes. Apply that convention when asked to publish; an explicit request for
a draft or prerelease takes precedence and needs corresponding verification.
The supplied verifier checks the normal published-release convention.

For a published release, require `/releases/tag/<version>` and check the
tag-keyed API. A missing release or a temporary draft URL calls for inspecting
the publisher and state. Preserve a deliberately requested draft; avoid
publishing it merely to make a URL check pass.

## Notes and evidence belong to this release

Sibling ROM names, save directories, platforms, controls, and driver fixes
are not WDC facts. Read the target revision's documentation and source.
`src/main.cpp` currently accepts a ROM path argument and stores configuration
under `build/user-data` relative to the working directory. A packaged launch
must account for that behavior; copying a sibling's double-click instructions
can give players the wrong ROM or save location.

The notes helper rejects empty text, automated placeholders, the template's
angle-bracket tokens, and undefined full or collapsed reference links. The
agent still checks the prose, evidence, and links before using
`gh release edit --notes-file`.
