---
name: release
description: Cut a wasm-cxx-shim release — version bump, CHANGELOG, doc-staleness sweep, PR (and post-merge, tag + GitHub Release)
user-invocable: true
---

# Release — wasm-cxx-shim

Drives the mechanical parts of a release end-to-end. Pauses at each
human-decision point (CHANGELOG content review, PR body sign-off,
post-merge tag) rather than barreling through.

Read [`CLAUDE.md`](../../../CLAUDE.md) first — the "Commit history
policy" and "All changes to main go through PRs" sections set the
ground rules this skill operates within.

## Arguments

- `<version>` (required): the version being released, no leading `v`.
  Examples: `0.4.0`, `0.4.0-alpha.1`, `0.4.0-alpha.1+5f95a3ac`. The
  CMake `project(VERSION ...)` line accepts only `MAJOR.MINOR.PATCH`,
  so for pre-releases the alpha/build-metadata segments live only on
  the git tag and the CHANGELOG title — CMakeLists.txt gets the bare
  numeric version.
- `prep`: run only phases 1-5 (version bump, CHANGELOG, sweep, build
  verify, push branch + open PR). Stop before the post-merge phases.
- `tag`: run only phases 6-7 (assumes the PR has merged; tags the
  merge commit, pushes, creates the GitHub Release).
- No phase flag: run `prep` (default).

## Pre-flight checks

Before doing anything, verify:

1. **On a feature branch**, not `main`. If on `main`, stop and tell
   the user to `git checkout -b release/v<version>` first.
2. **Working tree is clean** (`git status --porcelain` empty). If not,
   stop and surface the dirty paths.
3. **The version isn't already tagged.** `git rev-parse v<version>`
   should fail with "unknown revision." If it succeeds, stop —
   tagging the same version twice would be a bug.
4. **`gh` is authenticated** (`gh auth status`). The skill needs it
   for PR + release creation.

## Phases

### 1. Version bump

Update `CMakeLists.txt`'s `project(... VERSION X.Y.Z ...)` line. For
pre-releases, drop the alpha/build-metadata segments — CMake doesn't
parse them. Verify with `grep VERSION CMakeLists.txt | head -3`.

### 2. CHANGELOG

Update `CHANGELOG.md`:

- **Move the `## Unreleased` section** to a new versioned section
  (`## v<version> — <one-line summary> (<YYYY-MM-DD>)`). The
  one-line summary is yours to draft based on what's in the
  Unreleased section; surface it to the user for sign-off before
  finalizing.
- **Re-add an empty `## Unreleased`** above the new versioned
  section.
- **Update the "Tested upstream combinations" table** at the top
  with a new row for this version. Pull the manifold pin from
  `cmake/WasmCxxShimManifold.cmake`'s
  `_wasm_cxx_shim_manifold_default_manifold_tag` default, the
  Clipper2 pin (where applicable — for v0.4.0+ it's "inherits
  manifold's pin"), and the patch count (e.g., "1 (verbatim diff
  of #1690)").

### 3. Doc-staleness sweep

Run the sweep from the review skill's Cat 8:

```sh
grep -rn -E "v0\.[0-9]|pre-CI|TBD|TODO|coming|not started|in flight|NEXT" \
    --include='*.md' --exclude=CHANGELOG.md --exclude-dir=build .
```

Plus the API/patch-rename audit from Cat 8: if this release renames
or drops any `wasm_cxx_shim_add_manifold()` arguments, or shipped
patches under `cmake/manifold-patches/`, grep for the old names
across all `*.md` and surface hits.

Stop and walk the user through each hit: intentional historical
narrative or stale text? Apply the user's calls.

### 4. Build verify

Don't tag a release that doesn't build. Run:

```sh
rm -rf build/wasm32 && cmake --preset wasm32 -DWASM_CXX_SHIM_BUILD_MANIFOLD_LINK=ON \
    && cmake --build --preset wasm32 -j \
    && ctest --preset wasm32
```

All ctest entries must pass, including manifold-link and
manifold-tests. If anything fails, stop and surface — releasing on
red is never the right call.

### 5. PR

Per CLAUDE.md's "one session, one commit" + "All changes to main go
through PRs":

- If the branch has multiple commits, propose squashing them
  (`git reset --soft <last-pre-session> && git commit`). Stop and
  ask before force-resetting.
- Push the branch (`git push -u origin <branch>` for new branches,
  `git push --force-with-lease` for amended ones).
- Open the PR via `gh pr create`. The PR title is "Release
  v<version>" or similar; the body is **extracted from the
  CHANGELOG section you just wrote** (the versioned `## v<version>`
  block you moved Unreleased into). Surface the body for user
  sign-off before submitting.

**Stop here.** Per CLAUDE.md's "merge guardrail" — don't run `gh pr
merge`. Wait for the user to merge via the GitHub UI or their own
`gh pr merge`. CI must be green; a green CI is the END of the
prep flow, not a green light to merge.

### 6. Tag (post-merge)

After the user confirms the merge happened:

- `git checkout main && git pull && git remote prune origin`.
- `git tag v<version> <merge-commit-sha>`. Use the actual merge
  commit, not whatever `HEAD` happens to be.
- `git push origin v<version>`.

### 7. GitHub Release

```sh
gh release create v<version> \
    --title "v<version>" \
    --notes-file <(awk '/^## v<version>/,/^## v[^.]+\.[^.]+\.[^.]+/' CHANGELOG.md | head -n -1)
```

The awk extracts the section between `## v<version>` and the next
`## v...` header. Verify the extracted notes look right before
submitting; offer the user the rendered preview.

## Notes / gotchas

- **Pre-release versions**: CMake's `project(VERSION ...)` doesn't
  accept `0.4.0-alpha.1+5f95a3ac` — only `0.4.0`. The alpha/build
  metadata lives on the git tag (`v0.4.0-alpha.1+5f95a3ac`) and in
  the CHANGELOG title. Inside the project, version comparisons use
  the bare numeric version.
- **Tested-combinations table**: the column meanings shifted at
  v0.4.0-alpha.1. Pre-v0.4: the shim pre-declared Clipper2 with
  its own pin. v0.4+: manifold owns Clipper2's declaration, so the
  Clipper2 column reads "inherits manifold's pin" instead of a SHA.
  When updating, match the convention of the row above to stay
  consistent.
- **Carry-patch SHA in tested-combinations**: when a release
  vendors an upstream PR's diff (e.g., elalish/manifold#1690 in
  v0.4.0-alpha.1), the manifold pin includes the upstream commit
  the patch was generated against. Format: `<sha>` (master +
  vendored elalish/manifold#<n>).
- **Don't tag before merge.** If the branch hasn't merged yet,
  there's no merge commit to tag against. Tagging a feature branch
  ahead of merge produces a tag that points at a commit that
  later disappears from `main`'s history.
