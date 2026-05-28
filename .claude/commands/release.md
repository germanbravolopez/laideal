# /release — Ship a Release X.Y End-to-End

Executes the full release flow described in the **Development workflow** and **Release procedure** sections of the root `README.md`: prep the working branch, build artifacts, merge to `master` PR-style, tag the merge commit, and publish on GitHub.

Use when the user says "release X.Y", "ship X.Y", "cut release X.Y". Reject if no version is given — ask for it.

The root `README.md` is the source of truth. If a step here ever drifts from the README, the README wins; update this skill afterwards.

---

## Preconditions (check before doing anything)

1. **Current branch is not `master`.** Releases are cut from a working branch (`develop` or `feature/...`) and merged into `master` — never committed straight to it. If on `master`, stop and ask the user which branch to release from.
2. **Working tree is clean** (`git status` has no unstaged/untracked changes). If dirty, ask the user whether to commit, stash, or abort. Do not silently include stray edits in the release commit.
3. **Version argument is in `X.Y` form** (e.g. `8.2`, not `v8.2` or `8.2.0`). It must be **greater** than the current `project(laideal VERSION ...)` in `CMakeLists.txt`. If equal or lower, abort.
4. **No existing artifacts for this version.** `releases\old_releases\X.Y.zip` and `releases\setup_outputs\laideal_setup_X.Y.exe` must not exist. `release.ps1` aborts on its own if they do, but flag it upfront so the user can confirm overwrite vs. bump.
5. **No existing `X.Y` git tag.** `git tag -l X.Y` must be empty.

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

### 3. Build the artifacts (README "Release procedure")

```powershell
.\releases\release.ps1 X.Y
```

This runs configure -> build -> `windeployqt` -> zip -> Inno Setup, end-to-end. The script aborts upfront if `CMakeLists.txt` does not match `X.Y` or if a zip/installer for `X.Y` already exists, so failures here usually mean step 1 was incomplete.

Wait for it to finish and verify both artifacts exist:
- `releases\old_releases\X.Y.zip`
- `releases\setup_outputs\laideal_setup_X.Y.exe`

If the script fails, **stop**. Do not merge to `master`. Diagnose with the user, fix on the working branch, amend or add a follow-up commit, and rerun.

### 4. Merge PR-style to `master` (README step 4)

```powershell
git checkout master
git merge --no-ff <working-branch> -m "Merge branch '<working-branch>' for release X.Y"
git push origin master
```

`--no-ff` is mandatory — the project's convention is that each release shows up as a single discrete merge point on `master`'s history. Do **not** fast-forward.

If the user prefers a GitHub PR instead of a local merge, stop here and tell them: open the PR for `<working-branch>` -> `master`, use the default "Create a merge commit" button (not "Squash" or "Rebase"), then resume at step 5 once `master` is updated locally (`git checkout master && git pull`).

### 5. Tag the merge commit (README step 5)

```powershell
git tag -a X.Y -m "Release X.Y"
git push origin X.Y
```

The tag points at the merge commit on `master`, not at the version-bump commit on the working branch. Master itself was pushed in step 4; only the tag is pushed here.

### 6. Publish on GitHub (README step 6)

Requires the GitHub CLI. The release body should be **only the new `X.Y` section** of `releases_notes.txt`, converted to markdown — not the whole accumulated file. Extract it to a temp `.md` file, publish, then delete the temp file.

`releases_notes.txt` uses plain Markdown-compatible indentation (top-level bullets at column 0, nested bullets at 2 spaces) so the section body can be lifted straight into the GitHub release body. The only conversion step is to drop the bare `X.Y` title line — GitHub already shows the tag name as the release title — which the regex already does by capturing only what follows the title line.

```powershell
$ver = 'X.Y'
$tmpNotes = Join-Path $env:TEMP "laideal_release_$ver.md"
$raw = Get-Content releases_notes.txt -Raw
# Capture from the bare 'X.Y' line up to (but not including) the next bare 'N.M' line or EOF.
$m = [regex]::Match($raw, "(?ms)^$([regex]::Escape($ver))\r?\n(.*?)(?=^\d+\.\d+\r?\n|\z)")
if (-not $m.Success) { throw "No section for $ver found in releases_notes.txt" }
# Body is already Markdown-indented (column-0 bullets, 2-space nesting); just trim trailing blanks.
$body = $m.Groups[1].Value.TrimEnd()
Set-Content -Path $tmpNotes -Value $body -Encoding utf8

gh release create $ver `
    releases/old_releases/$ver.zip `
    releases/setup_outputs/laideal_setup_$ver.exe `
    --title "Release $ver" `
    --notes-file $tmpNotes

Remove-Item $tmpNotes
```

Verify visually (or with `gh release view X.Y`) that the body renders as a clean bulleted list. If the regex misses (e.g. the section uses an unexpected separator) or the list looks wrong, fall back to passing `--notes-file releases_notes.txt` once, then edit the release on GitHub.

### 7. Post-release housekeeping

- Confirm `git status` on `master` is clean and on the merge commit.
- Surface the GitHub release URL from `gh release create` output.
- Switch back to the working branch so future commits don't land on `master`:
  ```powershell
  git checkout <working-branch>
  ```
- If the user wants the working branch deleted or renamed for the next iteration, ask — don't delete branches unprompted.

---

## Rules

- **Never skip `--no-ff`** on the merge. Linear/fast-forward history defeats the "release = single point on master" convention.
- **Never commit to `master` directly.** Even if a last-minute fix is needed mid-release, commit it on the working branch and re-merge.
- **One release at a time.** If two unrelated changesets are sitting on the working branch, ask the user whether to split them into two releases or bundle.
- **Stop on the first failure.** Build error, missing doc, dirty tree, tag conflict — surface it and wait, don't paper over.
- **English in `releases_notes.txt`** (and everywhere else in the repo). Inno Setup and the GitHub release both surface this file to end users — keep it customer-readable, not changelog jargon.

---

## When NOT to use this skill

- Just bumping `CMakeLists.txt` — that's part of release, but if the user only wants the bump (not the merge/tag/publish), do that one step.
- "Release notes" updates without an actual release — just edit `releases_notes.txt` directly.
- Hotfix that ships outside the normal flow — ask first; the workflow assumes a clean working-branch -> master merge.
