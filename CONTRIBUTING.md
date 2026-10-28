# Contributing to La Ideal

La Ideal is maintained by one developer, mostly working with AI coding agents (Claude Code), for one shop. The code is public, but the shop's data never is: no database, log, backup or settings file of the shop goes into the repository, an issue or a test.

## Before you start

- **Build**: see [README §Build](README.md#build) (Qt 6.4.3 + MinGW 11.2 + Ninja, or Qt Creator).
- **Coding rules**: [`.claude/skills/coding-guidelines/SKILL.md`](.claude/skills/coding-guidelines/SKILL.md) - English identifiers and comments (Spanish only for what the user sees), Qt and SQL conventions.
- **Where things are**: [`docs/INDEX.md`](docs/INDEX.md), and for AI agents [`docs/ai_agent_instructions.md`](docs/ai_agent_instructions.md) (read first) and [`CLAUDE.md`](CLAUDE.md).
- **What is open**: [`docs/progress_tracker.md`](docs/progress_tracker.md). Finished work goes to [`docs/completed_milestones.md`](docs/completed_milestones.md).

## Every change

1. Work on `develop`, or on a short-lived branch cut from it and named after the change (`feature/<topic>`, `fix/<topic>`, `docs/<topic>`, `tests/<topic>`, `ci/<topic>`); a fix for the released version is a `hotfix/X.Y` branch cut from `master`. Never commit on `master` (see the workflow below).
2. Cover the change with a test: a unit or end-to-end suite (`tests/`), or a UI smoke step (`tests/ui/`) for a window or flow. How to run each suite: [README §Tests](README.md#tests); how to write one: [`docs/testing/`](docs/testing/README.md).
3. Anything touching invoices, the `verifactu_*` columns, ticket numbering, row edits / deletes or the printed QR also goes through the Verifactu compliance review ([`docs/modules/verifactu/verifactu-requirements.md`](docs/modules/verifactu/verifactu-requirements.md); the `verifactu-compliance-auditor` agent).
4. Build, run `ctest` until it is green, and update the docs ([`/update-docs`](.claude/skills/update-docs/SKILL.md) checklist).
5. Commit with a single-line message in the project's style (see `git log`).

---

## Development workflow

Two long-lived branches:

| Branch | Holds | Moves by |
|---|---|---|
| `develop` | the work in progress, the base of every change | direct commits, or pull requests from short-lived branches |
| `master` | only released code: every commit on it is a published release `X.Y` | pull requests only (protected: nobody pushes, admins included) |

Short-lived branches are named after what they change: `feature/<topic>`, `fix/<topic>`, `docs/<topic>`, `tests/<topic>`, `ci/<topic>` (cut from `develop`), and `hotfix/X.Y` (cut from `master`, see [Hotfix](#hotfix-a-fix-for-the-released-version)).

**When the code is built and tested** (`.github/workflows/ci.yml`, one run per commit, a newer push cancels the obsolete run):

| Event | `build` (compile + `ctest`) | `ui-smoke` (installed app, blocks A-E) | `release` |
|---|---|---|---|
| Pull request to `develop` | yes | yes | - |
| Push to `develop` (direct commit or merged PR) | yes | - | - |
| Pull request to `master` | yes | yes | **dry run**: version, notes and installer checked, nothing published |
| Push to `master` (only ever a merged PR) | yes | - | **publishes** `X.Y` from the exe this run tested |
| Manual (`gh workflow run ci.yml --ref <branch>`, or Actions -> CI -> *Run workflow*) | yes | yes | - |

A pull request into `master` can only merge when `build`, `ui-smoke` and `release` are green; GitHub enforces it.

### A change (feature, fix, docs)

1. Start from an up-to-date `develop`:
   ```powershell
   git checkout develop
   git pull --ff-only origin develop
   git checkout -b fix/search-accents     # or commit straight on develop for a small change
   ```
2. Implement it with its test (see [Every change](#every-change)). Commit as you go - small, focused commits with single-line messages in the project style (see `git log`). The branch also carries its documentation updates (`docs/progress_tracker.md`, `docs/completed_milestones.md`, the relevant `docs/modules/*.md`, `docs/architecture.md`).
3. Open a pull request to `develop`; CI runs `build` and `ui-smoke` on it. Merge it when green, with a merge commit, and delete the branch:
   ```powershell
   git push -u origin fix/search-accents
   gh pr create --base develop --title "<what it changes>" --body "<why>"
   gh pr merge --auto --merge --delete-branch     # merges by itself once the checks pass
   ```
   Without the GitHub CLI: push the branch, open the pull request on github.com (*Compare & pull request*, base `develop`), wait for the checks and press *Merge pull request* (*Create a merge commit*), then *Delete branch*.

### Release X.Y

A release is a pull request from `develop` to `master`. Its merge is the release: the push to `master` it causes builds, tests, tags `X.Y` on the merge commit and publishes the GitHub Release. There is no separate tag step. The `/release` skill drives these steps.

1. On `develop` (directly, or on a `release/X.Y` branch merged into `develop` first), make it **release-ready**:
   - `CMakeLists.txt` is bumped to `project(laideal VERSION X.Y ...)`, higher than the latest release.
   - `releases_notes.txt` (English) **and** `releases_notes_es.txt` (Spanish) both have a new `X.Y` section at the top with the customer-facing changes. Inno Setup shows the file for the language picked at install time and the GitHub Release body (what the in-app updater shows) is built from both. Keep both files UTF-8 **with BOM** so the installer renders accents.
   - The docs are current - the `/update-docs` checklist: `docs/progress_tracker.md` (Current Status points at `X.Y`), `docs/completed_milestones.md` (the top section suffixed `(shipped in X.Y)`), `docs/architecture.md`, the relevant `docs/modules/*.md`, `docs/INDEX.md`, the root `README.md`, and this file if the workflow changed.
   - When the release changes invoicing or the Verifactu records, the declaración responsable date (`src/verifactu/producerdeclaration.cpp`) is the release day.
   Commit it as a single `release X.Y - <headline>` commit. Locally, build and run the suites the release touches; CI runs the full set.
2. Open the pull request `develop` -> `master`:
   ```powershell
   git push origin develop
   gh pr create --base master --head develop --title "Release X.Y" `
       --body "Release X.Y - see the X.Y section of releases_notes.txt / the GitHub release."
   ```
   CI runs `build`, `ui-smoke` and the `release` dry run (the installer is kept as the `installer` artifact of the run, to try by hand). **In parallel**, review the diff (`/code-review`) and, when it touches invoices, ticket numbering, the `verifactu_*` columns or the QR, run the Verifactu compliance review. After the UI smoke test, look through its `ui-smoke` artifact (part 1 of [`docs/testing/smoke_test.md`](docs/testing/smoke_test.md)). A finding is fixed on `develop`; the pull request picks it up.
3. Merge with a merge commit (NOT squash, NOT rebase), keeping `develop`:
   ```powershell
   gh pr merge --auto --merge
   ```
   or on github.com: *Merge pull request* -> *Create a merge commit* (do **not** delete `develop`). GitHub refuses the merge until the three checks are green.
4. The push to `master` runs `build` + `release`, which publishes `X.Y`. Watch it: `gh run watch` (or the Actions tab), then `gh release view X.Y` shows the zip, the installer and its `.sha256`.
5. Bring the merge commit back into `develop`, so both branches share it:
   ```powershell
   git checkout develop
   git pull --ff-only origin develop
   git merge --ff-only origin/master
   git push origin develop
   ```
   If `develop` moved during the release, drop `--ff-only` (a merge commit). (On github.com: a pull request `master` -> `develop`, merged with a merge commit.)
6. After installing, the shop runs *Comprobar tras actualizar* (`laideal.exe --self-check`, part 2 of the smoke test).

### Hotfix: a fix for the released version

`develop` may hold unreleased work; a hotfix must ship only the fix. It is cut from `master`, which always holds exactly the released code.

1. Branch from `master`:
   ```powershell
   git checkout master
   git pull --ff-only origin master
   git checkout -b hotfix/X.Y      # X.Y = the hotfix version, e.g. 12.4
   ```
2. Fix the root cause with a regression test that fails without the fix ([`/debugging`](.claude/skills/debugging/SKILL.md)); run the affected suites locally. Bump `CMakeLists.txt` to `X.Y`, add one `X.Y` bullet to both notes files, and commit.
3. Open the pull request `hotfix/X.Y` -> `master`. CI runs `build`, `ui-smoke` and the `release` dry run in parallel with a review of the (small) diff; the Verifactu compliance review only when the fix touches invoices, numbering or the QR.
4. Merge it (`gh pr merge --auto --merge --delete-branch`, or the web button): the push to `master` publishes `X.Y`.
5. Bring the fix into `develop`, where it can conflict with newer work:
   ```powershell
   git checkout develop
   git pull --ff-only origin develop
   git merge origin/master -m "Merge master (hotfix X.Y) into develop"
   git push origin develop
   ```
   The next release from `develop` must use a higher version than the hotfix.

### Branch protection (GitHub settings)

- `master`: pull request required (0 approvals: one maintainer), required checks `build`, `ui-smoke` and `release`, branch up to date before merging, rules applied to administrators too, no force push, no deletion.
- `develop`: no force push, no deletion; direct pushes allowed.
- Repository: auto-merge allowed; branches are not deleted automatically on merge (that would delete `develop`).

If GitHub Actions is down and a release cannot wait, an administrator can switch off *Do not allow bypassing the above settings* for `master` (Settings -> Branches), merge, publish with the [local fallback](#local-fallback-releasesreleaseps1), and switch it back on.

---

## Release procedure

This section covers the **build, installer, and publish step**. The end-to-end flow (branching, version bump, pull request, merge) lives in [Development workflow](#development-workflow) above.

### Automated (default): merging into `master` publishes

The `release` job of `.github/workflows/ci.yml` (`needs: build`) packages **the exe the `build` job of the same run compiled and tested** - no second compile - with the one packaging recipe `releases\package.ps1`, shared with the UI smoke test and the local fallback:

1. (build job) Configure -> build (Release) -> **`ctest` (hard gate: a failing test stops the run before `release`)** -> uploads `laideal.exe` as the `laideal` artifact.
2. (release job) Reads `X.Y` from `project(laideal VERSION X.Y ...)` in `CMakeLists.txt` and refuses a version that is already released or not higher than the latest release.
3. Extracts the `X.Y` section of `releases_notes_es.txt` and `releases_notes.txt` (fails if either is missing or empty) into a bilingual body: Spanish, then English (the in-app updater shows only the Spanish section).
4. `releases\package.ps1`: refuses a UI-test build (`--fake-aeat`), `windeployqt` (and asserts the MinGW + Qt runtime DLLs, the SQLite driver and the TLS backend were deployed), zip, Inno Setup installer and its `.sha256`.
5. On a pull request to `master` it stops here (dry run; the installer is the `installer` artifact). On the push to `master` it creates the GitHub Release `X.Y` on that commit (which creates the tag) with `X.Y.zip` + `laideal_setup_X.Y.exe` + `laideal_setup_X.Y.exe.sha256`. A re-run of a half-failed publishing run replaces the release on the same commit.

The updater's contract: a non-draft, non-pre-release GitHub release (`/releases/latest` ignores both, so there is no beta channel and a lower tag reads "ya tiene la más reciente"), tagged exactly `X.Y`, with an asset `laideal_setup_X.Y.exe` and its `laideal_setup_X.Y.exe.sha256`. The updater refuses an installer whose size or SHA-256 does not match.

Toolchain caching: every job sets up Qt, MinGW 11.2 and Ninja through `.github/actions/setup-toolchain` (Qt and MinGW cached between runs), and the compiling jobs reuse a ccache compile cache, so a run mostly recompiles only what changed. `ctest` runs four suites at a time (the runner has four cores): the suite stage takes about as long as the slowest suite, `aeat_direct_backend`.

### Local fallback: `releases\release.ps1`

The same pipeline (configure -> build -> `ctest` -> `releases\package.ps1`) runs in one command via `releases\release.ps1`, for offline builds or when you need the installer without publishing. No Qt cmd prompt needed - the script loads the Qt environment itself. It does **not** publish to GitHub.

**Release toolchain** (pinned by `releases\release.ps1`):

| Tool | Expected location |
|------|-------------------|
| Qt (MinGW 64-bit) | `C:\Qt\6.4.3\mingw_64` |
| CMake | `C:\Qt\Tools\CMake_64` |
| MinGW | `C:\Qt\Tools\mingw1120_64` |
| Ninja | `C:\Qt\Tools\Ninja` |
| Inno Setup 6 | `C:\Program Files (x86)\Inno Setup 6\ISCC.exe` |
| PowerShell | 5.1+ (ships with Windows 10/11) |

To repoint at a different Qt / CMake / Ninja install, edit the `$QtBinDir` / `$CMakeBinDir` / `$MingwBinDir` / `$NinjaBinDir` constants at the top of `releases\release.ps1`.

From any PowerShell prompt in the repo root, on the commit to release (normally `master` after the release merge):

```powershell
.\releases\release.ps1 X.Y
```

(or from cmd: `releases\release.bat X.Y`). The script aborts up front if the supplied version does not match `CMakeLists.txt`, or if a staging dir for that version already exists.

Artifacts in `build-release\` (gitignored, wiped on each run): `X.Y.zip`, `laideal_setup_X.Y.exe` and `laideal_setup_X.Y.exe.sha256`.

To publish them by hand (only when the CI `release` job could not), with the [GitHub CLI](https://cli.github.com/) - the same body and assets the job publishes; `--target master` creates the `X.Y` tag on the release merge commit:

```powershell
$ver = 'X.Y'
# Spanish first, then English: the bilingual body the release job publishes.
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

Without the CLI: github.com -> Releases -> *Draft a new release*, tag `X.Y` on `master`, title `Release X.Y`, paste the two sections (`## Español`, then `## English`) and attach the three files.
