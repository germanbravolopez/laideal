# La Ideal

Desktop management software for a dry-cleaning and laundry shop. Built with **C++17 + Qt** (5.15+/6.x) on Windows, using a local SQLite database.

---

## Features

- **Client management** — directory with name, phone, and address
- **Garment receipts** — multi-garment tickets per client visit; quantity, size, service, and price per line
- **Garment pickup** — mark items as paid and collected; edit sizes, prices, and observations; split garment rows
- **Printing** — receipt and full invoice layouts built as Excel files and printed via an external script
- **Supplier invoices** — invoice entry form with automatic IVA/base calculation
- **Accounting reports** — monthly, quarterly, and annual HTML reports; period locking to prevent back-dated entry
- **Catalogue management** — garments, clients, suppliers, and services via a generic filterable list viewer with PDF export
- **Verifactu** — AEAT mandatory digital invoicing integration (8.0+, required for Spanish businesses from 2026)
- **In-app updater** — checks GitHub Releases at startup and on demand (Ayuda → Buscar actualizaciones); downloads the installer and replaces the running version in place. Toggle the startup check in Configuración.
- **In-app release notes** — Ayuda → Notas de la versión shows the bundled `releases_notes.txt` with the full version history, offline.

---

## Requirements

| Dependency | Version |
|------------|---------|
| Qt | 5.15+ or 6.x |
| Qt modules | Widgets, Sql, PrintSupport, Network |
| CMake | 3.5+ |
| Compiler | C++17-compatible (MSVC or MinGW) |
| OS | Windows 10 / 11 |

---

## Build

From a PowerShell prompt in the repo root:

```powershell
# Add Qt + MinGW + CMake + Ninja to PATH for this shell session
$env:PATH = "C:\Qt\Tools\Ninja;C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\mingw1120_64\bin;C:\Qt\6.4.3\mingw_64\bin;$env:PATH"

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The executable lands at `build\src\app\laideal.exe`. Qt Creator users can also open `CMakeLists.txt` directly and build with the *Release* configuration - it handles its own toolchain setup.

> The Ninja generator (not MinGW Makefiles) is used deliberately. `mingw32-make` writes per-recipe temp `.bat` files under `%TEMP%` and shells out to `cmd.exe`; on systems where antivirus / Controlled Folder Access blocks executing scripts from `%TEMP%`, every parallel job dies with `Access is denied (e=5)`. Ninja calls compilers directly via `CreateProcess`, so it avoids the issue and is also faster.

### Debug in VSCode

The repo ships `.vscode/{settings,launch,tasks}.json` for debugging via the CMake Tools and C/C++ extensions. Open the workspace and press **F5**: the build task compiles `laideal` in `build-debug/` (CMAKE_BUILD_TYPE=Debug, separate from the Release `build/` so the two coexist), then gdb (`C:\Qt\Tools\mingw1120_64\bin\gdb.exe`) launches the exe — breakpoints set in the editor are honored, step-into skips Qt internals. Default launch config is **Debug laideal (MinGW gdb)**; the alternative **Debug current CMake launch target** follows whatever target you select in the CMake Tools status bar.

If you installed Qt to a non-default path, edit the hardcoded `C:\Qt\...` entries in `.vscode/settings.json` and `.vscode/launch.json` to match.

---

## Configuration

All configuration is managed through a single JSON file at `~/.laideal_settings.json` (home directory). No source-code edits are required.

**First run**: if the database path is not set, the app opens the Settings dialog automatically before showing the main window.

**Ongoing**: open Archivo → Configuración... to change any setting at runtime.

| Setting | Key in JSON | Notes |
|---------|-------------|-------|
| Database path | `db.path` | Full path to the SQLite `.db` file |
| IVA rate | `app.ivaRate` | Default VAT percentage (e.g. `21`) |
| Report output paths | `report.*` | Directories for monthly/quarterly/annual HTML reports |
| Business name / address / city | `business.*` | Printed on receipts and invoices |
| Verifactu NIF | `verifactu.nif` | Issuer tax ID (NIF) |
| Verifactu company name | `verifactu.name` | Issuer name as registered with AEAT |
| Verifactu ServiceKey | `verifactu.serviceKey` | Obtain at https://facturae.irenesolutions.com/verifactu/go |
| Verifactu environment | `verifactu.production` | `false` = TESTING, `true` = production |

The application icon is embedded in the executable (Windows resource + Qt resource) so no `.ico` path needs to be configured by the user.

---

## Documentation

Full technical documentation lives in [`docs/`](./docs/README.md).

| Document | Description |
|----------|-------------|
| [Architecture](./docs/architecture.md) | Module overview, DB schema, data flow, known issues |
| [Module docs](./docs/README.md) | One reference page per module |
| [Verifactu](./docs/modules/verifactu/README.md) | AEAT digital invoicing — setup, API, DB schema |
| [Progress tracker](./docs/progress_tracker.md) | Open issues, blocking items, roadmap |
| [Quick-find index](./docs/INDEX.md) | Every file, function, concept — Ctrl+F entry point |

---

## Known issues

See [docs/progress_tracker.md](./docs/progress_tracker.md) for the full list. Blocking issues for the current release are at the top.

---

## Development workflow

**Never commit directly to `master`.** `master` only ever moves through merges from a working branch, and every commit on `master` corresponds to a release tag.

1. Create or check out a working branch (e.g. `develop`, or a feature branch like `feature/fix-search`):
   ```powershell
   git checkout -b develop
   ```
2. Implement your changes on the branch. Commit as you go - small, focused commits with single-line messages in the project style (see `git log`). The branch is also where the documentation updates live - keep `docs/progress_tracker.md` (Completed Milestones / Open Issues), the relevant `docs/modules/*.md` and `docs/architecture.md` in sync with the code as you go.
3. Before merging to `master`, the branch must be **release-ready**:
   - All planned changes are applied and reviewed.
   - The project builds cleanly (`releases\release.ps1 <next-version>` succeeds end-to-end, including `windeployqt` and Inno Setup).
   - All documentation is up to date - run the `/update-docs` skill or follow its checklist by hand: `docs/progress_tracker.md`, `docs/architecture.md`, the relevant `docs/modules/*.md`, `docs/INDEX.md`, and the root `README.md` if a user-visible behaviour or build/release step changed.
   - `CMakeLists.txt` is bumped to the new `project(laideal VERSION X.Y ...)`.
   - `releases_notes.txt` has a new X.Y section at the top with the customer-facing changes (Inno Setup shows this file at install time).
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
6. Publish on GitHub - see [Release procedure](#release-procedure) below for the artifacts and the GitHub publish step.

After publishing, the working branch can either continue (rename + reuse for the next iteration) or be deleted. Either way, the next set of changes starts again at step 1.

---

## Release procedure

This section covers only the **build and installer step**. The end-to-end flow (branching, version bump, merge, tag, push, GitHub publish) lives in [Development workflow](#development-workflow) above.

The release pipeline (configure -> build -> `windeployqt` -> zip -> Inno Setup installer) runs in one command via `releases\release.ps1`. No Qt cmd prompt needed - the script loads the Qt environment itself.

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

(or from cmd: `releases\release.bat X.Y`). The script aborts up front if the supplied version does not match `CMakeLists.txt`, or if a zip/installer for that version already exists.

Artifacts:

- `releases\old_releases\X.Y.zip` - portable build (exe + Qt runtime)
- `releases\setup_outputs\laideal_setup_X.Y.exe` - Inno Setup installer

These are what you attach to the GitHub release (step 6 of Development workflow). With the [GitHub CLI](https://cli.github.com/) installed, the publish step is:

```powershell
$ver = 'X.Y'
# Extract just the new X.Y section from releases_notes.txt into a temp .md so the
# GitHub release body shows only this release's bullets, not the whole accumulated file.
# The file uses Markdown-compatible indentation (top-level bullets at column 0,
# nested at 2 spaces), so no de-indenting is needed - just drop the title line
# (the regex captures only what follows it) and trim trailing blanks.
$tmpNotes = Join-Path $env:TEMP "laideal_release_$ver.md"
$raw = Get-Content releases_notes.txt -Raw
$m = [regex]::Match($raw, "(?ms)^$([regex]::Escape($ver))\r?\n(.*?)(?=^\d+\.\d+\r?\n|\z)")
Set-Content -Path $tmpNotes -Value $m.Groups[1].Value.TrimEnd() -Encoding utf8

gh release create $ver `
    releases/old_releases/$ver.zip `
    releases/setup_outputs/laideal_setup_$ver.exe `
    --title "Release $ver" `
    --notes-file $tmpNotes

Remove-Item $tmpNotes
```

---

## Version history

See [`releases_notes.txt`](./releases_notes.txt).
