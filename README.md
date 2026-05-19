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

```bash
git clone <repo-url>
cd laideal
cmake -B build
cmake --build build --config Release
```

Or open `CMakeLists.txt` directly in Qt Creator and build with the *Release* configuration.

---

## Configuration

All configuration is managed through a single JSON file at `~/.laideal_settings.json` (home directory). No source-code edits are required.

**First run**: if the database path is not set, the app opens the Settings dialog automatically before showing the main window.

**Ongoing**: open Archivo → Configuración... to change any setting at runtime.

| Setting | Key in JSON | Notes |
|---------|-------------|-------|
| Database path | `db.path` | Full path to the SQLite `.db` file |
| App icon path | `app.iconPath` | Path to `.ico` or `.png` |
| IVA rate | `app.ivaRate` | Default VAT percentage (e.g. `21`) |
| Report output paths | `report.*` | Directories for monthly/quarterly/annual HTML reports |
| Business name / address / city | `business.*` | Printed on receipts and invoices |
| Verifactu NIF | `verifactu.nif` | Issuer tax ID (NIF) |
| Verifactu company name | `verifactu.name` | Issuer name as registered with AEAT |
| Verifactu ServiceKey | `verifactu.serviceKey` | Obtain at https://facturae.irenesolutions.com/verifactu/go |
| Verifactu environment | `verifactu.production` | `false` = TESTING, `true` = production |

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

## Deploy

Package the application for distribution using **windeployqt** and **Inno Setup**:

- [windeployqt guide](https://medium.com/swlh/how-to-deploy-your-qt-cross-platform-applications-to-windows-operating-system-by-using-windeployqt-a7cd5663d46e)
- [Inno Setup installer tutorial](https://www.youtube.com/watch?v=Y9Ovo2XJHDs)
- Installer tool: [Inno Setup](https://jrsoftware.org/isinfo.php)

## Release procedure

1. Update the version number in `CMakeLists.txt`
2. Update `releases_notes.txt`
3. Build with the *Release* configuration in Qt Creator
4. Close Qt Creator
5. Open a Qt command prompt with administrator rights
6. `cd C:\Users\gebra\work\tintoreria\laideal\releases`
7. Run `deploy_laideal_run_in_qt_cmd.bat`
8. Enter the release tag when prompted (e.g. `8.0`)
9. Update the application icon after installation if needed

---

## Version history

See [`releases_notes.txt`](./releases_notes.txt).
