# Contributing to La Ideal

La Ideal is maintained by one developer, mostly working with AI coding agents (Claude Code), for one shop. The code is public, but the shop's data never is: no database, log, backup or settings file of the shop goes into the repository, an issue or a test.

## Before you start

- **Build**: see [README §Build](README.md#build) (Qt 6.4.3 + MinGW 11.2 + Ninja, or Qt Creator).
- **Coding rules**: [`.claude/skills/coding-guidelines/SKILL.md`](.claude/skills/coding-guidelines/SKILL.md) - English identifiers and comments (Spanish only for what the user sees), Qt and SQL conventions.
- **Where things are**: [`docs/INDEX.md`](docs/INDEX.md), and for AI agents [`docs/ai_agent_instructions.md`](docs/ai_agent_instructions.md) (read first) and [`CLAUDE.md`](CLAUDE.md).
- **What is open**: [`docs/progress_tracker.md`](docs/progress_tracker.md). Finished work goes to [`docs/completed_milestones.md`](docs/completed_milestones.md).

## Every change

1. Work on `develop` or a `feature/...` branch, never on `master` (see the workflow below).
2. Cover the change with a test: a unit or end-to-end suite (`tests/`), or a UI smoke step (`tests/ui/`) for a window or flow. How to run each suite: [README §Tests](README.md#tests); how to write one: [`docs/testing/`](docs/testing/README.md).
3. Anything touching invoices, the `verifactu_*` columns, ticket numbering, row edits / deletes or the printed QR also goes through the Verifactu compliance review ([`docs/modules/verifactu/verifactu-requirements.md`](docs/modules/verifactu/verifactu-requirements.md); the `verifactu-compliance-auditor` agent).
4. Build, run `ctest` until it is green, and update the docs ([`/update-docs`](.claude/skills/update-docs/SKILL.md) checklist).
5. Commit with a single-line message in the project's style (see `git log`).

---

## Development workflow

**Never commit directly to `master`.** `master` only ever moves through merges from a working branch, and every commit on `master` corresponds to a release tag.

1. Create or check out a working branch (e.g. `develop`, or a feature branch like `feature/fix-search`):
   ```powershell
   git checkout -b develop
   ```
2. Implement your changes on the branch. Commit as you go - small, focused commits with single-line messages in the project style (see `git log`). The branch is also where the documentation updates live - keep `docs/progress_tracker.md` (status / open issues), `docs/completed_milestones.md`, the relevant `docs/modules/*.md` and `docs/architecture.md` in sync with the code as you go.
3. Before merging to `master`, the branch must be **release-ready**:
   - All planned changes are applied and reviewed.
   - The project builds cleanly (`releases\release.ps1 <next-version>` succeeds end-to-end, including `windeployqt` and Inno Setup).
   - `ctest` and the UI smoke test (`ui-smoke.yml`) are green, and the manual smoke test ([`docs/testing/smoke_test.md`](docs/testing/smoke_test.md)) is done.
   - All documentation is up to date - run the `/update-docs` skill or follow its checklist by hand: `docs/progress_tracker.md`, `docs/completed_milestones.md`, `docs/architecture.md`, the relevant `docs/modules/*.md`, `docs/INDEX.md`, the root `README.md` if a user-visible behaviour or the build changed, and this file if the workflow or release steps changed.
   - `CMakeLists.txt` is bumped to the new `project(laideal VERSION X.Y ...)`.
   - `releases_notes.txt` (English) **and** `releases_notes_es.txt` (Spanish) both have a new X.Y section at the top with the customer-facing changes. Inno Setup shows the file for the language picked at install time; CI fails the release if either section is missing. Keep both files UTF-8 **with BOM** so the installer renders accents.
   - `docs/progress_tracker.md` Current Status points at the new release; the milestone entry covers the work delivered.
   Commit the version bump + notes on the branch as a single `release X.Y` commit.
4. Merge the branch to `master` through a pull request, with a merge commit so the release shows up as a single point on `master`'s history. `master` has a branch protection rule that requires a PR, so this is the path of least resistance. With the [GitHub CLI](https://cli.github.com/):
   ```powershell
   git push -u origin develop
   gh pr create --base master --head develop `
       --title "Release X.Y" `
       --body "Release X.Y - see ``releases_notes.txt`` X.Y section / GitHub release for the changelog."
   gh pr merge develop --merge --admin --delete-branch=false
   git checkout master
   git pull --ff-only origin master
   ```
   `--merge` produces the merge commit (NOT `--squash`, NOT `--rebase`). `--admin` lets the merge proceed despite required-reviews on a solo project. `--delete-branch=false` keeps `develop` alive for the next cycle.

   Fallback if `gh` is unavailable or the PR can't merge cleanly, do it locally:
   ```powershell
   git checkout master
   git merge --no-ff develop -m "Merge branch 'develop' for release X.Y"
   git push origin master
   ```
   The push will be flagged as `Bypassed rule violations` but admins can override - decide whether to tighten or relax the rule afterwards.

   The merge commit on `master` is the release commit that gets tagged in the next step.
5. Tag the merge commit on `master` and push the tag:
   ```powershell
   git tag -a X.Y -m "Release X.Y"
   git push origin X.Y
   ```
6. **Pushing the tag in step 5 is the publish trigger** - the `release` job in `.github/workflows/ci.yml` runs on any `X.Y` tag, after the `build` job (incl. the `ctest` gate) passes, and packages + publishes the GitHub Release automatically (artifacts + notes) reusing the exe the build job produced. Just watch the run (`gh run watch` or the Actions tab); no manual build or `gh release create` needed. See [Release procedure](#release-procedure) below for what the workflow does and the local fallback.

After publishing, the working branch can either continue (rename + reuse for the next iteration) or be deleted. Either way, the next set of changes starts again at step 1.

---

## Release procedure

This section covers the **build, installer, and publish step**. The end-to-end flow (branching, version bump, merge, tag, push) lives in [Development workflow](#development-workflow) above.

### Automated (default): push the tag, CI does the rest

The release is the second stage of the CI workflow: `.github/workflows/ci.yml` has a `release` job that `needs: build` and runs only on an `X.Y` tag push (`if: startsWith(github.ref, 'refs/tags/')`). So a tag push runs the normal `build` job (configure -> build -> **`ctest` hard gate**), and only if that passes does the `release` job package + publish on a `windows-latest` runner - **reusing the exe the build job already produced instead of recompiling**:

1. (build job) Configure -> build (Release) -> **`ctest` (hard gate: a failing test aborts before the release job runs)** -> uploads `laideal.exe` as the `laideal` artifact.
2. (release job) Validates the tag equals `project(laideal VERSION X.Y ...)` in `CMakeLists.txt` (fails fast on mismatch).
3. Installs Qt 6.4.3 MinGW (via `jurplel/install-qt-action`, for `windeployqt` + the runtime DLLs) and Inno Setup 6 - no rebuild.
4. Downloads the `laideal` artifact -> stage `laideal.exe` -> `windeployqt` (and asserts the MinGW + Qt runtime DLLs were deployed) -> zip -> Inno Setup installer.
5. Extracts the `X.Y` section from `releases_notes_es.txt` and `releases_notes.txt` (fails if either is missing/empty) and publishes a GitHub Release `X.Y` with a bilingual body (Spanish, then English - also what the in-app updater shows) with `X.Y.zip` + `laideal_setup_X.Y.exe` attached and that section as the body. Idempotent: a pre-existing release for the tag is replaced (the tag is kept).

So the normal release is just `git tag X.Y && git push origin X.Y` (step 5 of Development workflow). Watch it with `gh run watch` or the Actions tab. The published assets and the bare `X.Y` tag are what the in-app updater consumes - no manual `gh release create`.

### Local fallback: `releases\release.ps1`

The same pipeline (configure -> build -> `windeployqt` -> zip -> Inno Setup installer) runs in one command via `releases\release.ps1` for offline builds or when you need the artifacts without publishing. No Qt cmd prompt needed - the script loads the Qt environment itself. It does **not** publish to GitHub; attach the artifacts manually (see the `gh release create` snippet at the end of this section).

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

From any PowerShell prompt in the repo root (after `CMakeLists.txt` has been bumped to the target version on the working branch):

```powershell
.\releases\release.ps1 X.Y
```

(or from cmd: `releases\release.bat X.Y`). The script aborts up front if the supplied version does not match `CMakeLists.txt`, or if a staging dir for that version already exists.

Artifact:

- `build-release\laideal_setup_X.Y.exe` - Inno Setup installer (the only artifact; `build-release\` is gitignored and wiped on each run, so nothing accumulates in `releases\`)

When using this local fallback, attach it manually (the automated workflow above does it for you). With the [GitHub CLI](https://cli.github.com/) installed, the publish step mirrors what the `ci.yml` release job runs:

```powershell
$ver = 'X.Y'
# Extract just the new X.Y section from both notes files into a temp .md so the
# GitHub release body shows only this release's bullets - Spanish first, then
# English, the same bilingual body the ci.yml release job publishes (and the
# in-app updater shows). The files use Markdown-compatible indentation, so the
# regex just drops the version line and the rest is trimmed.
$tmpNotes = Join-Path $env:TEMP "laideal_release_$ver.md"
function Get-Section([string]$file) {
    $raw = Get-Content $file -Raw
    $m = [regex]::Match($raw, "(?ms)^$([regex]::Escape($ver))\r?\n(.*?)(?=^r?\d+\.\d+\r?\n|\z)")
    if (-not $m.Success) { throw "No section for $ver found in $file" }
    return $m.Groups[1].Value.TrimEnd()
}
$body = "## Español`n`n$(Get-Section 'releases_notes_es.txt')`n`n## English`n`n$(Get-Section 'releases_notes.txt')"
Set-Content -Path $tmpNotes -Value $body -Encoding utf8

gh release create $ver `
    build-release/laideal_setup_$ver.exe `
    --title "Release $ver" `
    --notes-file $tmpNotes

Remove-Item $tmpNotes
```
