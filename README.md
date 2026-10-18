# La Ideal

[![CI](https://github.com/germanbravolopez/laideal/actions/workflows/ci.yml/badge.svg)](https://github.com/germanbravolopez/laideal/actions/workflows/ci.yml)
[![Latest release](https://img.shields.io/github/v/release/germanbravolopez/laideal)](https://github.com/germanbravolopez/laideal/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/germanbravolopez/laideal/total)](https://github.com/germanbravolopez/laideal/releases)
![Top language](https://img.shields.io/github/languages/top/germanbravolopez/laideal)
![Platform](https://img.shields.io/badge/platform-Windows-0078D6?logo=windows&logoColor=white)
[![License](https://img.shields.io/badge/license-Proprietary-red)](./License.txt)

Desktop management software for a dry-cleaning and laundry shop. Built with **C++17 + Qt** (5.15+/6.x) on Windows, using a local SQLite database.

---

## Features

- **Client management** — directory with name, phone, and address
- **Garment receipts** — multi-garment tickets per client visit; quantity, size, service, and price per line
- **Garment pickup** — mark items as paid and collected; edit sizes, prices, and observations; split garment rows
- **Printing** — receipts and invoices printed as direct ESC/POS over the Windows RAW spooler (10.0+, no Excel/Office dependency); configurable printer queue and 58/80 mm paper width, with an optional Epson Status API layer for paper-out / cover-open detection
- **Supplier invoices** — invoice entry form with automatic IVA/base calculation
- **Accounting reports** — monthly, quarterly, and annual HTML reports; period locking to prevent back-dated entry
- **Catalogue management** — garments, clients, suppliers, and services via a generic filterable list viewer with PDF export
- **Verifactu** — AEAT mandatory digital invoicing integration (8.0+, required for Spanish businesses from 2026)
- **In-app updater** — checks GitHub Releases at startup and on demand (Ayuda → Buscar actualizaciones); downloads the installer and replaces the running version in place. Toggle the startup check in Configuración.
- **In-app release notes** — Ayuda → Notas de la versión shows the full version history, offline, in the configured language (`releases_notes.txt` English / `releases_notes_es.txt` Spanish, both bundled).

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

There are four build folders, one per purpose. Each is a separate CMake tree, so they coexist (all are git-ignored by `build*`):

| Folder | What it builds | For | How |
|--------|----------------|-----|-----|
| `build/` | The app (Release) and the test suites | Running the app, `ctest` | [Release build](#release-build) below |
| `build-debug/` | The app (Debug) | Debugging in VSCode (F5) | [Debug in VSCode](#debug-in-vscode) below |
| `build-uitest/` | The **UI-test** app (`-DLAIDEAL_UI_TEST=ON`, with the `--fake-aeat` switch) and the tools `fake_aeat_server` / `escpos_preview` | The UI smoke test and the AEAT pre-production dry run on this PC | [UI-test build](#ui-test-build) below |
| `build-release/` | The installer `laideal_setup_X.Y.exe` (clean configure, `windeployqt`, Inno Setup) | A release without CI | `releases\release.ps1 X.Y` - see [CONTRIBUTING.md](./CONTRIBUTING.md#local-fallback-releasesreleaseps1) |

Every command below runs from a PowerShell prompt in the repo root, after putting the toolchain on `PATH` for the session:

```powershell
$env:PATH = "C:\Qt\Tools\Ninja;C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\mingw1120_64\bin;C:\Qt\6.4.3\mingw_64\bin;$env:PATH"
```

> The Ninja generator (not MinGW Makefiles) is used deliberately. `mingw32-make` writes per-recipe temp `.bat` files under `%TEMP%` and shells out to `cmd.exe`; on systems where antivirus / Controlled Folder Access blocks executing scripts from `%TEMP%`, every parallel job dies with `Access is denied (e=5)`. Ninja calls compilers directly via `CreateProcess`, so it avoids the issue and is also faster.

### Release build

```powershell
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release    # once (configure)
cmake --build build                                   # the app + every test suite
ctest --test-dir build --output-on-failure            # run the test suites
```

The executable lands at `build\src\app\laideal.exe`. Qt Creator users can also open `CMakeLists.txt` directly and build with the *Release* configuration - it handles its own toolchain setup.

### Debug in VSCode

The repo ships `.vscode/{settings,launch,tasks}.json` for debugging via the CMake Tools and C/C++ extensions.

**Press F5** (launch config **Debug laideal (MinGW gdb)**): the build task configures `build-debug/` if needed (its `dependsOn` runs "CMake: configure (Debug)" first — CMake no-ops an already-valid cache, so it's free on repeat runs) and compiles `laideal`, then gdb (`C:\Qt\Tools\mingw1120_64\bin\gdb.exe`) launches the exe — breakpoints set in the editor are honored, step-into skips Qt internals. The Debug tree is separate from the Release `build/` so the two coexist. The alternative config **Debug current CMake launch target** follows whatever target you select in the CMake Tools status bar.

The terminal equivalent (with the toolchain on `PATH` as above):

```powershell
cmake -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=C:/Qt/6.4.3/mingw_64
cmake --build build-debug --target laideal
```

If you installed Qt to a non-default path, edit the hardcoded `C:\Qt\...` entries in `.vscode/settings.json` and `.vscode/launch.json` to match.

**Troubleshooting — F5 fails / no `laideal.exe` gets built:** a stale half-configured `build-debug/` (holds `CMakeCache.txt` + `CMakeFiles/` but **no `build.ninja`**, cache shows `CMAKE_MAKE_PROGRAM-NOTFOUND`) from a `cmake` configure that ran without `C:\Qt\Tools\Ninja` on PATH. Fix: `Remove-Item -Recurse -Force build-debug`, then F5 again (the configure step re-runs with the correct PATH). A healthy Debug tree always contains `build.ninja`.

### UI-test build

Needed only to run the UI smoke test or the AEAT pre-production dry run on this PC (CI builds its own). Never install it at the shop: a release refuses an exe with the `--fake-aeat` switch, and CMake warns while the option is ON. Keep it in this separate folder, because the option is cached per folder.

```powershell
cmake -B build-uitest -G Ninja -DCMAKE_BUILD_TYPE=Release -DLAIDEAL_UI_TEST=ON   # once (configure)
cmake --build build-uitest --target laideal fake_aeat_server escpos_preview     # after each change
```

The test scripts are Python (pywinauto) and run from a virtual environment in `.venv/` (git-ignored), created once:

```powershell
python -m venv .venv
.venv\Scripts\python.exe -m pip install -r tests\ui\requirements.txt   # again after requirements.txt changes
```

Then run them as listed in [Tests](#tests).

### Tests

Four kinds of test, from the fastest to the one that needs a person. Each row links to the document with the full steps; the commands run from the repository root.

| Suite | Where it runs | How to run it | Options | Steps |
|-------|---------------|---------------|---------|-------|
| **Unit + end-to-end suites** (`tests/test_*.cpp`, CTest: logic, DB functions, the app's windows against a fake Verifactu / AEAT server) | Locally after any build; CI (`ci.yml`) on every push / PR, and as the release gate | `ctest --test-dir build --output-on-failure` | `-R <regex>` runs some suites (e.g. `-R e2e`); `aeat_xsd` is skipped unless Python has `lxml` (`python -m pip install lxml`) | [docs/testing/README.md](docs/testing/README.md#running-the-tests) |
| **UI smoke test, CI** (`tests/ui/smoke.py`: the installed app driven like a person, blocks A-E, against a fake AEAT) | GitHub `windows-latest` runner, about 9 min | `gh workflow run ui-smoke.yml --ref <branch>`, then `gh run watch` | Also runs on pushes touching `tests/ui/` or `tests/tools/`; results in the job summary and the `ui-smoke` artifact | [docs/testing/ui_smoke.md](docs/testing/ui_smoke.md#where-it-runs) |
| **UI smoke test, local** (the same script on this PC; it takes the mouse and keyboard for about 4 min) | This PC, from the [UI-test build](#ui-test-build) and its `.venv` | `.venv/Scripts/python tests/ui/smoke.py --exe build-uitest/src/app/laideal.exe --tools build-uitest/tests` | `--blocks AB` (some blocks), `--printer LaIdeal-Smoke` (block C, after `tests/ui/printer.py install`), `--installer <setup.exe>` (E4, disposable machines only), `--out <folder>` | [docs/testing/ui_smoke.md](docs/testing/ui_smoke.md#running-it-locally) |
| **AEAT pre-production run** (`tests/ui/aeat_preproduction.py`: paid ticket, two payments, cancellation, R5 rectifications, export, against AEAT's test environment) | **Locally only**, on a PC with the issuer's FNMT certificate | `.venv/Scripts/python tests/ui/aeat_preproduction.py --exe <laideal.exe> --settings-from %USERPROFILE%/work/tintoreria/documents/prueba-aeat-settings.json --out <new folder>` | `--dry-run --tools build-uitest/tests` runs it against the fake AEAT with the UI-test build (no certificate; CI does this on every UI smoke run) | [docs/testing/ui_smoke.md](docs/testing/ui_smoke.md#the-aeat-pre-production-run-local) |
| **Manual smoke test** (the shop's real schema, the physical printer, how screens / PDFs / installer pages look) | This PC, before each release, about 15 min | Follow the checklist | M6 only when the release changes the direct AEAT client | [docs/testing/smoke_test.md](docs/testing/smoke_test.md) |

Before the local rows, have the toolchain `PATH` line of [Build](#build) in the session. The local UI smoke test and the pre-production dry run need the [UI-test build](#ui-test-build); every Python row needs its `.venv`. The real pre-production run drives a normal build of the branch (`build\src\app\laideal.exe`). How to write tests, and every suite, are in [docs/testing/](docs/testing/README.md).

---

## Configuration

All configuration is managed through a single JSON file at `~/.laideal_settings.json` (home directory). No source-code edits are required.

**First run**: if the database path is not set, the app opens the Settings dialog automatically before showing the main window.

**Ongoing**: open Archivo → Configuración... to change any setting at runtime.

| Setting | Key in JSON | Notes |
|---------|-------------|-------|
| Language | `app.language` | `es` or `en` — language of Qt's standard dialogs (Sí/No, Aceptar/Cancelar, etc.) and of Ayuda → Notas de la versión. Applied live when Configuración is accepted. On first run it takes the language chosen in the installer (default `es`). The rest of the app is still Spanish |
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

Full technical documentation lives in [`docs/`](./docs/README.md), and is also published as a
searchable site at <https://germanbravolopez.github.io/laideal/> (MkDocs Material → GitHub Pages,
deployed by [`.github/workflows/docs.yml`](./.github/workflows/docs.yml) on push to `master`).

To preview the site locally:

```powershell
pip install ".[docs]"
mkdocs serve   # http://127.0.0.1:8000
```

| Document | Description |
|----------|-------------|
| [Architecture](./docs/architecture.md) | Module overview, DB schema, data flow, known issues |
| [Module docs](./docs/README.md) | One reference page per module |
| [Verifactu](./docs/modules/verifactu/README.md) | AEAT digital invoicing — setup, API, DB schema |
| [Progress tracker](./docs/progress_tracker.md) | Open issues, blocking items, roadmap |
| [Completed milestones](./docs/completed_milestones.md) | History of finished work, newest first |
| [Quick-find index](./docs/INDEX.md) | Every file, function, concept — Ctrl+F entry point |

---

## Known issues

See [docs/progress_tracker.md](./docs/progress_tracker.md) for the full list. Blocking issues for the current release are at the top.

---

## Contributing

How to work on the code, test it, and cut a release (branching, version bump, merge, tag, the CI release job and the local `release.ps1` fallback): [CONTRIBUTING.md](./CONTRIBUTING.md).

---

## Version history

See [`releases_notes.txt`](./releases_notes.txt) for the english version or [`releases_notes_es.txt`](./releases_notes_es.txt) for the spanish one.
