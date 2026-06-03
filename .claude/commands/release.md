# /release — Ship a Release X.Y End-to-End

Executes the full release flow described in the **Development workflow** and **Release procedure** sections of the root `README.md`: prep the working branch, merge to `master` PR-style, then tag the merge commit — which triggers `.github/workflows/release.yml` to build the artifacts and publish the GitHub Release automatically. The build + package + publish steps are no longer run by hand; the skill drives the merge/tag and then watches CI.

Use when the user says "release X.Y", "ship X.Y", "cut release X.Y". Reject if no version is given — ask for it.

The root `README.md` is the source of truth. If a step here ever drifts from the README, the README wins; update this skill afterwards.

---

## Preconditions (check before doing anything)

1. **Current branch is not `master`.** Releases are cut from a working branch (`develop` or `feature/...`) and merged into `master` — never committed straight to it. If on `master`, stop and ask the user which branch to release from.
2. **Working tree is clean** (`git status` has no unstaged/untracked changes). If dirty, ask the user whether to commit, stash, or abort. Do not silently include stray edits in the release commit.
3. **Version argument is in `X.Y` form** (e.g. `8.2`, not `v8.2` or `8.2.0`). It must be **greater** than the current `project(laideal VERSION ...)` in `CMakeLists.txt`. If equal or lower, abort.
4. **`releases_notes.txt` has an `X.Y` section.** `release.yml` now **fails the publish** if the section is missing or empty (it becomes the GitHub release body), so this is a hard gate, not just a nicety. Verify it before tagging. (Local-artifact files `releases\old_releases\X.Y.zip` / `setup_outputs\laideal_setup_X.Y.exe` only matter for the local fallback in step 8b — CI builds fresh on a runner.)
5. **No existing `X.Y` git tag and no existing `X.Y` GitHub release.** `git tag -l X.Y` must be empty; `gh release view X.Y` must 404. (The workflow can replace an existing release, but for a clean cut both should be absent.)

If any precondition fails, stop and surface the problem — don't try to "fix it up" without asking.

---

## Steps

### 1. Pre-flight the release-ready checklist (README step 3)

The working branch must be release-ready before the version-bump commit. Verify each item; if missing, fix it and stage the change for the bump commit (do not commit yet):

- [ ] **`CMakeLists.txt`** — `project(laideal VERSION X.Y ...)` bumped to the target version.
- [ ] **`releases_notes.txt`** — a new `X.Y` section at the **top** with customer-facing changes in **English** (Inno Setup shows this file to end users at install time; same file is reused for the GitHub release body in step 6). Match the existing entries' tone. If missing, draft one from `git log` since the previous release tag and ask the user to confirm/edit before committing.
- [ ] **`docs/progress_tracker.md`** — Current Status points at the new release; the milestone entry covers what shipped. Add a "previous release" pointer to the prior version.
- [ ] **`docs/` and `README.md`** — run the `/update-docs` checklist mentally; if anything user-visible, build-related, or workflow-related changed since the last release tag, the docs must reflect it. If nothing user-visible changed, this is a no-op.

### 2. Commit the version bump

Stage `CMakeLists.txt`, `releases_notes.txt`, `docs/progress_tracker.md`, and any other docs touched in step 1. Commit as a **single** commit, single-line message in project style:

```
release X.Y - <one-line summary of what this release ships>
```

The summary should mention the headline customer-facing change(s). Example from history: `release 8.1 - quick-turn bugfix / UX release on top of 8.0: ...`.

### 3. Sanity build + tests (hard gate before opening the PR)

A plain Release build into the normal `build\` directory - same incantation the README documents - followed by the CTest suite. Cheap, fast, and catches both the "merged a branch that doesn't compile" foot-gun and any regression the tests cover before bothering reviewers or the release-artifact pipeline.

```powershell
# Add Qt + MinGW + CMake + Ninja to PATH for this shell session.
$env:PATH = "C:\Qt\Tools\Ninja;C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\mingw1120_64\bin;C:\Qt\6.4.3\mingw_64\bin;$env:PATH"

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected output: `build\src\app\laideal.exe` exists, the build ends with no errors, and `ctest` reports `100% tests passed`. Warnings are tolerated; errors and test failures are not. The user may run this via Qt Creator instead - that's equivalent.

If the build **or any test** fails, **stop**. Fix on the working branch, commit, and rerun. Do **not** open the PR with broken code or a failing test. (CI enforces the same gate: `release.yml` runs `ctest` between Build and Stage, so a failing test aborts the publish even if this local step is skipped.)

This step does **not** produce the customer-facing zip / installer - that happens in step 8, after merge + tag, so the released artifacts come from the tagged merge commit on `master` rather than from the working-branch tip.

### 4. Open a GitHub PR to `master` (README step 4)

`master` has a branch protection rule that requires changes to come through a pull request (since the repo went public in 8.3). Push the working branch and open the PR — but do **not** merge yet; step 5 reviews the diff first.

```powershell
# Push the working branch first so the PR has something to point at.
git push -u origin <working-branch>

# Open the PR. Title and body are short; the GitHub release in step 9 carries
# the full notes, so the PR body just points there.
gh pr create --base master --head <working-branch> `
    --title "Release X.Y" `
    --body "Release X.Y - see ``releases_notes.txt`` X.Y section / GitHub release for the changelog."
```

Capture the PR number from `gh pr create`'s output (or `gh pr view --json number -q .number`) — step 5 needs it.

### 5. Review the PR at high effort before merging (NEW)

A release merge is the last point at which something can be caught before it goes out to customers with a tagged version and a GitHub installer. Run a high-effort review on the PR diff — not the default level. The diff of a release is usually wider than a single feature PR (the working branch may have accumulated weeks of changes), so the broader-coverage tiers are appropriate even though they may surface lower-confidence findings.

```
/code-review high <PR#>
```

`high` widens coverage relative to the default `medium` while keeping the false-positive rate manageable — the right tradeoff for the release gate on a small solo project. `max` was tried for the 8.4 gate and produced too much volume for the gain over `high`; `max` / `ultra` (the cloud multi-agent variant) remain available if a particular release ever warrants a deeper sweep. Runs locally, not billed.

**What to do with the findings**:
- **Genuine bugs / regressions / release-blockers**: fix on the working branch, push, and the PR updates automatically. Re-run the review if the fixes are non-trivial. Do **not** merge until they are addressed.
- **Low-confidence or nitpick findings**: surface them to the user with a one-line summary and let the user decide per-item. Don't silently dismiss anything that touches Verifactu, AEAT submission, accounting totals, or DB writes — those are the high-blast-radius areas where false-positive triage should err on the side of fixing.
- **Findings outside the release scope** (pre-existing issues, unrelated modules): file them as new entries in `docs/progress_tracker.md` Open Non-Blocking Issues, do not block the release on them.

Only proceed to step 6 once the user has confirmed the review is clean (or that remaining findings are intentionally deferred).

### 6. Merge the PR

**First, wait for GitHub Actions CI to pass on the pushed commit — always, without being asked.** The local sanity build in step 3 is not a substitute: CI builds on a clean `windows-latest` runner and runs the same `ctest` gate, and is the last automated check before the release becomes a tagged, public installer. Do not merge a red or still-running CI. Watch the `ci.yml` run for the working-branch tip and block on it:

```powershell
# Wait for the CI run on the just-pushed commit to finish green.
$rid = gh run list --workflow=ci.yml --branch <working-branch> --limit 1 --json databaseId -q '.[0].databaseId'
gh run watch $rid --exit-status   # non-zero exit = CI failed -> do NOT merge
```

`--exit-status` makes the command fail if CI failed. If CI is red, **stop**: read the failure, fix on the working branch, push, and re-wait — never merge around a failing build. Only once CI is green proceed to the merge:

```powershell
# Merge with --merge (the merge-commit option, not --squash and not --rebase) +
# --admin so the merge proceeds even if required-reviews / required-checks
# aren't satisfied. This is a solo project; the PR exists to honour the
# protection rule, not to gate on automated review.
gh pr merge <working-branch> --merge --admin --delete-branch=false

# Bring the merge commit into the local master so step 7 can tag it.
git checkout master
git pull --ff-only origin master
```

`--merge` is mandatory — `--squash` or `--rebase` defeat the "release = single point on master" convention. `--delete-branch=false` keeps `<working-branch>` alive locally and on the remote for the next release cycle; do NOT pass `--delete-branch` unless the user asks.

Fallback if `gh` is unavailable or the PR can't merge cleanly: do the local merge instead (`git checkout master && git merge --no-ff <working-branch> -m "Merge branch '<working-branch>' for release X.Y" && git push origin master`). The push will succeed for an admin account but GitHub will log a `Bypassed rule violations` warning — flag it to the user when this happens so they can decide whether to tighten or relax the protection rule.

### 7. Tag the merge commit (README step 5)

```powershell
git tag -a X.Y -m "Release X.Y"
git push origin X.Y
```

The tag points at the merge commit on `master`, not at the version-bump commit on the working branch. Master itself was pushed by `gh pr merge` in step 6; only the tag is pushed here. **Pushing the tag is the publish trigger** — `.github/workflows/release.yml` starts on the `X.Y` tag and builds + packages + publishes the GitHub Release on its own. Steps 3 and 5 already validated the tagged code, so this is normally hands-off.

### 8. Watch the release workflow (it builds the artifacts and publishes)

The tag push in step 7 kicked off `release.yml`. Watch it to completion instead of building/publishing by hand:

```powershell
gh run watch (gh run list --workflow=release.yml --limit 1 --json databaseId -q '.[0].databaseId') --exit-status
```

On success it has built (CMake + Ninja, Release) -> `windeployqt` (with a runtime-DLL assertion) -> zipped -> compiled the Inno Setup installer -> created GitHub Release `X.Y` with `X.Y.zip` + `laideal_setup_X.Y.exe` attached and the `releases_notes.txt` `X.Y` section as the body. Confirm with `gh release view X.Y` that both assets are present and the body renders as a clean bulleted list.

**If the workflow fails**, read the failed step's log (`gh run view <id> --log-failed`) and diagnose with the user. Common causes: the tag doesn't match `CMakeLists.txt` (version step), no `X.Y` section in `releases_notes.txt` (notes step), or the `windeployqt` assertion tripped (deployment regression). Do **not** delete the tag without confirming — the merge commit is already public. Fix forward on the working branch; if the fix needs a new merge commit, re-tag is the user's call.

#### 8b. Local fallback (only if CI is unavailable)

If GitHub Actions can't run, build and publish locally from the tagged `master` commit. This is exactly what the workflow automates:

```powershell
.\releases\release.ps1 X.Y   # configure -> build -> windeployqt -> zip -> Inno Setup, into build-release\

$ver = 'X.Y'
$tmpNotes = Join-Path $env:TEMP "laideal_release_$ver.md"
$raw = Get-Content releases_notes.txt -Raw
$m = [regex]::Match($raw, "(?ms)^$([regex]::Escape($ver))\r?\n(.*?)(?=^\d+\.\d+\r?\n|\z)")
if (-not $m.Success) { throw "No section for $ver found in releases_notes.txt" }
Set-Content -Path $tmpNotes -Value $m.Groups[1].Value.TrimEnd() -Encoding utf8

gh release create $ver `
    releases/old_releases/$ver.zip `
    releases/setup_outputs/laideal_setup_$ver.exe `
    --title "Release $ver" `
    --notes-file $tmpNotes

Remove-Item $tmpNotes
```

### 9. Post-release housekeeping

- Confirm `git status` on `master` is clean and on the merge commit.
- Surface the GitHub release URL from `gh release create` output.
- Switch back to the working branch so future commits don't land on `master`:
  ```powershell
  git checkout <working-branch>
  ```
- If the user wants the working branch deleted or renamed for the next iteration, ask — don't delete branches unprompted.

---

## Rules

- **Always produce a merge commit on `master`** — `gh pr merge --merge` (preferred) or `git merge --no-ff` (fallback). Never `--squash`, `--rebase`, or fast-forward; those defeat the "release = single point on master" convention.
- **Never commit to `master` directly.** Even if a last-minute fix is needed mid-release, commit it on the working branch and re-merge.
- **One release at a time.** If two unrelated changesets are sitting on the working branch, ask the user whether to split them into two releases or bundle.
- **Stop on the first failure.** Build error, missing doc, dirty tree, tag conflict — surface it and wait, don't paper over.
- **English in `releases_notes.txt`** (and everywhere else in the repo). Inno Setup and the GitHub release both surface this file to end users — keep it customer-readable, not changelog jargon.

---

## When NOT to use this skill

- Just bumping `CMakeLists.txt` — that's part of release, but if the user only wants the bump (not the merge/tag/publish), do that one step.
- "Release notes" updates without an actual release — just edit `releases_notes.txt` directly.
- Hotfix that ships outside the normal flow — ask first; the workflow assumes a clean working-branch -> master merge.
