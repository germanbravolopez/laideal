# La Ideal

[![CI](https://github.com/germanbravolopez/laideal/actions/workflows/ci.yml/badge.svg)](https://github.com/germanbravolopez/laideal/actions/workflows/ci.yml)
[![Latest release](https://img.shields.io/github/v/release/germanbravolopez/laideal)](https://github.com/germanbravolopez/laideal/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/germanbravolopez/laideal/total)](https://github.com/germanbravolopez/laideal/releases)
![Top language](https://img.shields.io/github/languages/top/germanbravolopez/laideal)
![Platform](https://img.shields.io/badge/platform-Windows-0078D6?logo=windows&logoColor=white)
[![License](https://img.shields.io/badge/license-Proprietary-red)](./License.txt)

Desktop management software for a dry-cleaning and laundry shop. C++17 and Qt 6.4.3 on Windows (MinGW + Ninja), with a local SQLite database.

## Features

- **Clients, garments, tickets**: a ticket is one client visit with one row per garment (quantity, size, service, price).
- **Pickup**: mark items paid and collected, edit sizes, prices and observations, split garment rows.
- **Printing**: receipts and invoices as direct ESC/POS over the Windows RAW spooler (no Office dependency); printer queue, 58 / 80 mm paper, optional Epson Status API for paper-out and cover-open detection.
- **Supplier invoices**: entry form with automatic base / IVA split.
- **Accounting**: monthly, quarterly and annual PDF reports; quarter closing that blocks back-dated entries.
- **Catalogues**: garments, clients, suppliers and services in a generic filterable list with PDF export.
- **Verifactu** (AEAT digital invoicing): through the Irene Solutions gateway, or directly with AEAT using the owner's certificate. *Comprobar tras actualizar* (`--self-check`) checks an update on a copy of the newest backup.
- **Backups**: a daily and a manual SQLite backup, with an optional second folder.
- **Updater**: checks GitHub Releases at startup and on demand (Ayuda -> Buscar actualizaciones), downloads the installer and replaces the running version.
- **Release notes**: Ayuda -> Notas de la versión, offline, in the configured language.

## Requirements

| Dependency | Version |
|------------|---------|
| Qt | 6.4.3 (MinGW 64-bit); Qt 5 is not tested |
| Qt modules | Widgets, Sql, PrintSupport, Network (Test for the suites) |
| CMake | 3.5+ |
| Compiler | MinGW 11.2 (C++17), built with Ninja |
| OS | Windows 10 / 11 only (WinHTTP, CNG and DPAPI are used) |

## Build

`make.ps1` in the repository root wraps every command of this page and sets the toolchain `PATH` itself: `.\make <target>` from PowerShell or cmd. CI (`ci.yml`) runs the same targets.

| Command | Does |
|---------|------|
| `.\make help` | Lists the targets |
| `.\make build` / `.\make debug` | Release build into `build\` / Debug build into `build-debug\` |
| `.\make test` | Builds, then runs the suites; `-Filter <regex>` runs some, `-Jobs <n>` sets the parallelism |
| `.\make xsd` | The direct client's XML against AEAT's schemas (needs Python `lxml`) |
| `.\make uitest` / `.\make venv` | The UI-test build into `build-uitest\` / the Python environment of the UI scripts |
| `.\make printer` | Adds the capturing printer queue `LaIdeal-Smoke` (once, as administrator) |
| `.\make ui-smoke` | UI smoke test; takes the mouse and keyboard. `-Blocks AB`, `-Printer LaIdeal-Smoke`; with `-Exe <laideal.exe> -Tools <folder>` it tests an installed UI-test app (as CI does), `-Installer <setup.exe>` adds the reinstall step, `-Out <folder>` names the results folder |
| `.\make aeat-dry` | AEAT pre-production run against the fake AEAT, into `aeat-run\<date>-dry` |
| `.\make aeat-real -Settings <file>` | The same against AEAT's test environment with the issuer's certificate; asks before sending |
| `.\make regression` | `build`, `test`, `uitest`, `ui-smoke`, `aeat-dry` in a row; stops at the first failure |
| `.\make clean` / `.\make clean-all` | Deletes the test outputs (real AEAT runs are kept) / also the build folders (asks first) |

A release is not a target: see [CONTRIBUTING.md](./CONTRIBUTING.md#release-procedure).

### Build folders

Each folder is a separate CMake tree and all are git-ignored (`build*`).

| Folder | Builds | For |
|--------|--------|-----|
| `build/` | The app (Release) and the test suites | Running the app, `ctest` |
| `build-debug/` | The app (Debug) | Debugging in VSCode (F5) |
| `build-uitest/` | The UI-test app (`-DLAIDEAL_UI_TEST=ON`, with the `--fake-aeat` switch) and the tools `fake_aeat_server`, `escpos_preview` | The UI smoke test and the AEAT dry run on this PC |
| `build-release/` | The installer `laideal_setup_X.Y.exe` | A release without CI: `releases\release.ps1 X.Y` ([CONTRIBUTING.md](./CONTRIBUTING.md#local-fallback-releasesreleaseps1)) |

### By hand

Put the toolchain on `PATH` for the PowerShell session:

```powershell
$env:PATH = "C:\Qt\Tools\Ninja;C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\mingw1120_64\bin;C:\Qt\6.4.3\mingw_64\bin;$env:PATH"
```

Release build, from the repository root. The executable lands at `build\src\app\laideal.exe`. Qt Creator can open `CMakeLists.txt` and build the *Release* configuration instead.

```powershell
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build                                   # the app and every test suite
ctest --test-dir build --output-on-failure
```

Ninja is used on purpose: `mingw32-make` writes recipe scripts under `%TEMP%`, and where antivirus blocks executing from there every job fails with `Access is denied (e=5)`.

**Debug in VSCode**: `.vscode/` ships the launch and build tasks. F5 (*Debug laideal (MinGW gdb)*) configures `build-debug/` when needed, builds `laideal` and starts gdb. The terminal equivalent:

```powershell
cmake -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=C:/Qt/6.4.3/mingw_64
cmake --build build-debug --target laideal
```

If Qt is installed elsewhere, edit the `C:\Qt\...` paths in `.vscode/settings.json` and `.vscode/launch.json`. If F5 builds nothing, `build-debug/` was configured without Ninja on `PATH` (no `build.ninja`): delete it and press F5 again.

**UI-test build**: only for the UI smoke test and the AEAT dry run on this PC. Never install it at the shop: a release refuses an exe with the `--fake-aeat` switch. The Python scripts (pywinauto) run from `.venv/` (git-ignored).

```powershell
cmake -B build-uitest -G Ninja -DCMAKE_BUILD_TYPE=Release -DLAIDEAL_UI_TEST=ON
cmake --build build-uitest --target laideal fake_aeat_server escpos_preview
python -m venv .venv
.venv\Scripts\python.exe -m pip install -r tests\ui\requirements.txt
```

## Tests

| Kind | Run | Details |
|------|-----|---------|
| Unit and end-to-end suites (`tests/test_*.cpp`, 35 CTest tests: logic, DB functions, the app's windows against a fake Verifactu / AEAT server) | `ctest --test-dir build --output-on-failure`; `-R <regex>` runs some. `aeat_xsd` is skipped unless Python has `lxml` | [docs/testing/README.md](docs/testing/README.md#running-the-tests) |
| UI smoke test, CI (the installed app driven like a person, blocks A-E, against a fake AEAT) | `gh workflow run ci.yml --ref <branch>`, then `gh run watch`; also runs on every PR to `develop` and `master` | [docs/testing/ui_smoke.md](docs/testing/ui_smoke.md#where-it-runs) |
| UI smoke test, local (about 4 min, takes the mouse and keyboard) | `.\make ui-smoke` | [docs/testing/ui_smoke.md](docs/testing/ui_smoke.md#smoke-test-against-the-fake-aeat) |
| AEAT pre-production run (needs the issuer's FNMT certificate; locally only) | `.\make aeat-real -Settings <file>`; `.\make aeat-dry` runs it against the fake AEAT | [docs/testing/ui_smoke.md](docs/testing/ui_smoke.md#aeat-pre-production-run) |
| Smoke test (part 1: look of screens and PDFs, before a release; part 2: *Comprobar tras actualizar* on the shop PC) | The checklist; `laideal.exe --self-check` | [docs/testing/smoke_test.md](docs/testing/smoke_test.md) |

How to write tests and what each suite covers: [docs/testing/](docs/testing/README.md).

## Configuration

One JSON file, `~/.laideal_settings.json`, edited from Archivo -> Configuración. If the database path is not set, the app opens that dialog before the main window. `--settings <file>` starts the app with another file.

| Setting | Key | Notes |
|---------|-----|-------|
| Language | `app.language` | `es` or `en`: Qt's standard dialogs and the release notes (the rest of the app is Spanish). First run takes the installer's choice, default `es` |
| Database path | `database.path` | Full path of the SQLite file |
| Printing on / off | `print.enable` | `"Yes"` / `"No"` (text); guards the print call |
| Printer queue | `print.printer_name` | RAW target queue; empty = the Windows default printer |
| Paper width | `print.paper_width_mm` | 58 or 80 (default 80) |
| Epson Status API | `print.use_status_api` | `"Yes"` / `"No"` |
| Report root | `reports.root` | PDFs go to `Contabilidad`, `Listados/Prendas` and `Listados/Gastos` below it |
| Business | `business.name`, `.address`, `.city`, `.phone` | Printed on receipts and invoices |
| Issuer | `verifactu.nif`, `verifactu.company_name` | Tax ID and name as registered with AEAT |
| Connection | `verifactu.connection` | `irenesolutions` (gateway) or `aeat` (direct client) |
| Gateway | `verifactu.service_key`, `verifactu.environment` | Key stored encrypted (DPAPI), obtained at https://facturae.irenesolutions.com/verifactu/go; `TESTING` or `PRODUCTION` |
| Direct client | `verifactu.aeat_direct_production` | `false` = AEAT pre-production, `true` = production |
| Certificate | `verifactu.aeat_certificate_thumbprint`, `.aeat_certificate_file`, `.aeat_certificate_password` | Windows store thumbprint, or a .pfx file and its password (stored encrypted) |
| Startup recovery | `verifactu.pending_recovery_enabled`, `verifactu.pending_recovery_floor_date` | Whether the start lists PENDIENTE rows to resend, and the oldest date it considers |
| Update check | `updater.check_on_startup` | `true` by default |
| Backup | `backup.enabled`, `backup.second_folder` | `enabled` (default on) has no field in Configuración: JSON only. The second folder is edited in Configuración |

Written by the app, not to be edited: `verifactu.aeat_chain_marker`, `backup.last_time`, `backup.last_version`, `backup.last_error`, `app.last_started_version`.

The ingresos IVA rate is fixed at 21 % and is not a setting. Keys of older versions are removed on load. Key formats and Verifactu details: [docs/modules/verifactu/README.md](docs/modules/verifactu/README.md).

## Documentation

Technical documentation is in [`docs/`](./docs/README.md) and published at <https://germanbravolopez.github.io/laideal/> (MkDocs Material, deployed by `.github/workflows/docs.yml` on push to `master`). Preview locally with `pip install ".[docs]"` and `mkdocs serve`.

| Document | Content |
|----------|---------|
| [docs/architecture.md](./docs/architecture.md) | Modules, data flows, database schema |
| [docs/INDEX.md](./docs/INDEX.md) | Where to find any file, concept or topic |
| [docs/modules/verifactu/README.md](./docs/modules/verifactu/README.md) | AEAT digital invoicing |
| [docs/progress_tracker.md](./docs/progress_tracker.md) | Open issues and backlog |
| [docs/completed_milestones.md](./docs/completed_milestones.md) | Finished work, newest first |
| [CONTRIBUTING.md](./CONTRIBUTING.md) | Workflow, branching, release procedure |

Version history: [`releases_notes.txt`](./releases_notes.txt) (English), [`releases_notes_es.txt`](./releases_notes_es.txt) (Spanish).
