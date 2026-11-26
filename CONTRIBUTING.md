# Contributing to La Ideal

One maintainer, mostly working with AI coding agents (Claude Code), for one shop. The code is public; the shop's data never is: no database, log, backup or settings file of the shop goes into the repository, an issue or a test.

## Before you start

- Build and test commands: [README](README.md#build).
- Coding rules: [`.claude/skills/coding-guidelines/SKILL.md`](.claude/skills/coding-guidelines/SKILL.md).
- Where things are: [`docs/INDEX.md`](docs/INDEX.md); agents read [`docs/ai_agent_instructions.md`](docs/ai_agent_instructions.md) and [`CLAUDE.md`](CLAUDE.md) first.
- What is open: [`docs/progress_tracker.md`](docs/progress_tracker.md). Finished work goes to [`docs/completed_milestones.md`](docs/completed_milestones.md).

## Every change

1. Work on `develop`, or on a short-lived branch cut from it. Never commit on `master`.
2. Cover the change with a test: a unit or end-to-end suite (`tests/`), or a UI smoke step (`tests/ui/`) for a window or flow. Run: [README §Tests](README.md#tests). Write: [`docs/testing/`](docs/testing/README.md).
3. A change touching invoices, the `verifactu_*` columns, ticket numbering, row edits or deletes, or the printed QR also goes through the Verifactu compliance review ([`verifactu-requirements.md`](docs/modules/verifactu/verifactu-requirements.md), the `verifactu-compliance-auditor` agent).
4. Build, run `ctest` until green, update the docs (the [`/update-docs`](.claude/skills/update-docs/SKILL.md) checklist).
5. Commit with a single-line message in the style of `git log`.

---

## Development workflow

| Branch | Holds | Moves by |
|--------|-------|----------|
| `develop` | The work in progress, the base of every change | Direct commits, or pull requests from short-lived branches |
| `master` | Released code only: every commit is a published release `X.Y` | Pull requests only (protected, administrators included) |

Short-lived branches: `feature/<topic>`, `fix/<topic>`, `docs/<topic>`, `tests/<topic>`, `ci/<topic>` (from `develop`) and `hotfix/X.Y` (from `master`).

### What CI runs

`.github/workflows/ci.yml`, one run per commit (a newer push cancels the older run). Its build and test steps are the `make.ps1` targets used locally.

| Event | `build` (compile + `ctest`) | `ui-smoke` (installed app, blocks A-E) | `release` |
|-------|-----------------------------|----------------------------------------|-----------|
| Pull request to `develop` | yes | yes | - |
| Push to `develop` | yes (skipped by the `gate` job while the release PR is open on the same commit) | - | - |
| Pull request to `master` | yes | yes | dry run: nothing published |
| Push to `master` (a merged PR) | yes | - | publishes `X.Y` from the exe this run tested |
| Manual (`gh workflow run ci.yml --ref <branch>`) | yes | yes | - |

A pull request into `master` merges only when `build`, `ui-smoke` and `release` are green.

### A change

```powershell
git checkout develop
git pull --ff-only origin develop
git checkout -b fix/search-accents          # or commit on develop for a small change
# implement with its test; commit as you go; update the docs on the same branch
git push -u origin fix/search-accents
gh pr create --base develop --title "<what it changes>" --body "<why>"
gh pr merge --auto --merge --delete-branch  # merges once the checks pass
```

Without the GitHub CLI: open the pull request on github.com, wait for the checks, *Create a merge commit*, delete the branch.

### Release X.Y

A release is a pull request `develop` -> `master`. Merging it is the release: the push to `master` builds, tests, tags `X.Y` on the merge commit and publishes the GitHub Release. The `/release` skill drives these steps.

1. Make `develop` release-ready (`.\make regression` runs locally what CI runs on the PR):
   - `CMakeLists.txt`: `project(laideal VERSION X.Y ...)`, higher than the latest release.
   - `releases_notes.txt` (English) and `releases_notes_es.txt` (Spanish): a new `X.Y` section at the top with the customer-facing changes. Both files are UTF-8 with BOM so the installer shows accents.
   - Docs current per `/update-docs`: tracker Current Status points at `X.Y`, the top section of `completed_milestones.md` is suffixed `(shipped in X.Y)`.
   - If the release changes invoicing or the Verifactu records, the declaración responsable date (`src/verifactu/producerdeclaration.cpp`) is the release day.

   Commit it as one `release X.Y - <headline>` commit.
2. Open the pull request:
   ```powershell
   git push origin develop
   gh pr create --base master --head develop --title "Release X.Y" `
       --body "Release X.Y - see the X.Y section of releases_notes.txt / the GitHub release."
   ```
   CI runs `build`, `ui-smoke` and the `release` dry run (the installer is the run's `installer` artifact). Meanwhile review the diff (`/code-review`), run the Verifactu compliance review when the diff touches the areas of step 3 of "Every change", and read the `ui-smoke` artifact (part 1 of [`smoke_test.md`](docs/testing/smoke_test.md)). Fix findings on `develop`; the PR picks them up.
3. Merge with a merge commit (not squash, not rebase) and keep `develop`: `gh pr merge --auto --merge`, or *Create a merge commit* on github.com.
4. Watch the publishing run: `gh run watch`, then `gh release view X.Y` lists the zip, the installer and its `.sha256`.
5. Bring the merge commit back into `develop`:
   ```powershell
   git checkout develop
   git pull --ff-only origin develop
   git merge --ff-only origin/master
   git push origin develop
   ```
   If `develop` moved during the release, drop `--ff-only`.
6. After installing, the shop runs *Comprobar tras actualizar* (`laideal.exe --self-check`, part 2 of the smoke test).

### Hotfix

A hotfix ships only the fix, so it is cut from `master`.

```powershell
git checkout master
git pull --ff-only origin master
git checkout -b hotfix/X.Y
```

1. Fix the root cause with a regression test that fails without it ([`/debugging`](.claude/skills/debugging/SKILL.md)). Bump `CMakeLists.txt` to `X.Y`, add an `X.Y` entry to both notes files, commit.
2. Open the PR `hotfix/X.Y` -> `master`; review the diff while CI runs, plus the compliance review when it applies. Merge with `gh pr merge --auto --merge --delete-branch`: the push to `master` publishes `X.Y`.
3. Bring the fix into `develop` (it can conflict with newer work); the next release must use a higher version:
   ```powershell
   git checkout develop
   git pull --ff-only origin develop
   git merge origin/master -m "Merge master (hotfix X.Y) into develop"
   git push origin develop
   ```

### Branch protection (GitHub settings)

- `master`: pull request required (0 approvals), checks `build`, `ui-smoke` and `release` required, branch up to date before merging, applies to administrators, no force push, no deletion.
- `develop`: no force push, no deletion; direct pushes allowed.
- Repository: auto-merge allowed; branches are not deleted on merge automatically (that would delete `develop`).

If GitHub Actions is down and a release cannot wait, an administrator can switch off *Do not allow bypassing the above settings* for `master`, merge, publish with the [local fallback](#local-fallback-releasesreleaseps1) and switch it back on.

---

## Release procedure

The build, installer and publish step. The end-to-end flow is above.

### Automated: merging into `master` publishes

The `release` job of `ci.yml` (`needs: build`) packages the exe the `build` job of the same run compiled and tested, with `releases\package.ps1` (shared with the UI smoke test and the local fallback):

1. `build` job: configure, build (Release), `ctest` (a failing test stops the run before `release`), upload `laideal.exe`.
2. `release` job: reads `X.Y` from `CMakeLists.txt`; refuses a version already released or not higher than the latest.
3. Extracts the `X.Y` section of both notes files (fails if either is missing or empty) into a bilingual body, Spanish then English (the in-app updater shows only the Spanish part).
4. `package.ps1`: refuses a UI-test build (`--fake-aeat`), runs `windeployqt` (and asserts the MinGW and Qt runtime DLLs, the SQLite driver and the TLS backend are deployed), then makes the zip, the Inno Setup installer and its `.sha256`.
5. On a PR to `master` it stops (dry run). On the push to `master` it creates the GitHub Release `X.Y` on that commit, which creates the tag, with `X.Y.zip`, `laideal_setup_X.Y.exe` and `laideal_setup_X.Y.exe.sha256`. A re-run replaces the release on the same commit.

The updater's contract: a non-draft, non-pre-release release (`/releases/latest` ignores both, so there is no beta channel), tagged exactly `X.Y`, with the assets `laideal_setup_X.Y.exe` and `laideal_setup_X.Y.exe.sha256`. The updater refuses an installer whose size or SHA-256 differs.

Every job sets up Qt, MinGW 11.2 and Ninja through `.github/actions/setup-toolchain` (cached) and reuses a ccache. `ctest` runs four suites at a time.

### Local fallback: `releases\release.ps1`

The same pipeline (configure, build, `ctest`, `package.ps1`) in one command, for offline builds or an installer without publishing. It loads the Qt environment itself and does not publish.

| Tool | Location pinned by the script |
|------|-------------------------------|
| Qt (MinGW 64-bit) | `C:\Qt\6.4.3\mingw_64` |
| CMake | `C:\Qt\Tools\CMake_64` |
| MinGW | `C:\Qt\Tools\mingw1120_64` |
| Ninja | `C:\Qt\Tools\Ninja` |
| Inno Setup 6 | `C:\Program Files (x86)\Inno Setup 6\ISCC.exe` |

To use other locations, edit the `$QtBinDir`, `$CMakeBinDir`, `$MingwBinDir` and `$NinjaBinDir` constants at the top of the script. Run it on the commit to release (normally `master` after the merge):

```powershell
.\releases\release.ps1 X.Y        # or from cmd: releases\release.bat X.Y
```

It aborts if the version does not match `CMakeLists.txt` or a staging directory for it exists. Output in `build-release\` (git-ignored, wiped on each run): `X.Y.zip`, `laideal_setup_X.Y.exe`, `laideal_setup_X.Y.exe.sha256`.

To publish them by hand, only when the CI job could not (`--target master` creates the tag on the merge commit):

```powershell
$ver = 'X.Y'
$tmpNotes = Join-Path $env:TEMP "laideal_release_$ver.md"
function Get-Section([string]$file) {
    $raw = Get-Content $file -Raw
    $m = [regex]::Match($raw, "(?ms)^$([regex]::Escape($ver))\r?\n(.*?)(?=^\r?\d+\.\d+\r?\n|\z)")
    if (-not $m.Success) { throw "No section for $ver found in $file" }
    return $m.Groups[1].Value.TrimEnd()
}
$body = "## Español`n`n$(Get-Section 'releases_notes_es.txt')`n`n## English`n`n$(Get-Section 'releases_notes.txt')"
Set-Content -Path $tmpNotes -Value $body -Encoding utf8

gh release create $ver `
    build-release/$ver.zip `
    build-release/laideal_setup_$ver.exe `
    build-release/laideal_setup_$ver.exe.sha256 `
    --target master --title "Release $ver" --notes-file $tmpNotes

Remove-Item $tmpNotes
```

Without the CLI: github.com -> Releases -> *Draft a new release*, tag `X.Y` on `master`, title `Release X.Y`, the two sections (`## Español`, `## English`) as the body, and the three files attached.
