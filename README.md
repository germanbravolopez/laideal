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
# Add Qt + MinGW + CMake to PATH for this shell session
$env:PATH = "C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\mingw1120_64\bin;C:\Qt\6.4.3\mingw_64\bin;$env:PATH"

cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

The executable lands at `build\src\app\laideal.exe`. Qt Creator users can also open `CMakeLists.txt` directly and build with the *Release* configuration - it handles its own toolchain setup.

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

## Release procedure

The release pipeline (configure -> build -> `windeployqt` -> zip -> Inno Setup installer) runs in one command via `releases\release.ps1`. No Qt cmd prompt needed - the script loads the Qt environment itself.

**Release toolchain** (pinned by `releases\release.ps1`):

| Tool | Expected location |
|------|-------------------|
| Qt (MinGW 64-bit) | `C:\Qt\6.4.3\mingw_64` |
| CMake | `C:\Qt\Tools\CMake_64` |
| MinGW | `C:\Qt\Tools\mingw1120_64` |
| Inno Setup 6 | `C:\Program Files (x86)\Inno Setup 6\ISCC.exe` |
| PowerShell | 5.1+ (ships with Windows 10/11) |

To repoint at a different Qt / CMake version, edit the `$QtBinDir` / `$CMakeBinDir` / `$MingwBinDir` constants at the top of `releases\release.ps1`.

1. Bump the version in `CMakeLists.txt` (the `project(laideal VERSION X.Y ...)` line) and add the X.Y section to `releases_notes.txt`. Commit.
2. From any PowerShell prompt in the repo root:
   ```powershell
   .\releases\release.ps1 X.Y
   ```
   (or from cmd: `releases\release.bat X.Y`). The script aborts up front if the supplied version does not match `CMakeLists.txt`, or if a zip/installer for that version already exists.
3. After it finishes, the artifacts are at:
   - `releases\old_releases\X.Y.zip` - portable build (exe + Qt runtime)
   - `releases\setup_outputs\laideal_setup_X.Y.exe` - Inno Setup installer
4. Tag the release commit and push: `git tag -a X.Y -m "Release X.Y" && git push && git push origin X.Y`.
5. Publish to GitHub: open the repo on GitHub -> Releases -> Draft a new release -> select the X.Y tag -> attach `releases\old_releases\X.Y.zip` and `releases\setup_outputs\laideal_setup_X.Y.exe` -> paste the X.Y section from `releases_notes.txt` into the description. (If the [GitHub CLI](https://cli.github.com/) is installed, this collapses to `gh release create X.Y releases/old_releases/X.Y.zip releases/setup_outputs/laideal_setup_X.Y.exe --title "X.Y" --notes-file releases_notes.txt`.)

---

## Version history

See [`releases_notes.txt`](./releases_notes.txt).
