# Progress Tracker — La Ideal

**Rule**: Add new entries at the **top** of the relevant section. When entries exceed ~6 months old and are no longer actionable, move them to the Archive at the bottom. Keep the most important and recent information near the top.

---

## Current Status — May 2026

**Active branch**: `feature/add_mdiago_verifactu`
**Version**: 8.0 (unreleased — blocked by items below)

---

## Blocking Issues (must fix before merging `feature/add_mdiago_verifactu`)

- [ ] **Remove temp debug code** in `src/app/mainwindow.cpp` (near top of constructor)
  - Constructor calls `verifactuSubmitInvoice()` then `std::exit(0)`
  - App exits immediately on launch — cannot be used in this state
- [ ] **Persist Verifactu CSV to database** — the CSV received from AEAT after successful submission is logged via `qDebug()` but not saved anywhere
  - Needs `ALTER TABLE` to add columns (SQL in `docs/modules/verifactu/RESUMEN_IMPLEMENTACION.md`)
  - Then update `saveTicket()` or `verifactuSubmitInvoice()` to write the CSV
- [ ] **Add Verifactu columns to DB schema** — `ingresos` (or `facturas`) needs `verifactu_csv`, `verifactu_timestamp`, `verifactu_estado`, `verifactu_error`, `verifactu_url_qr`

---

## In Progress

- [ ] Verifactu integration — code complete, pending production testing
  - Classes: `VerifactuIntegration`, `VerifactuManager`, `VerifactuConfig`, `VerifactuInvoice`
  - TESTING environment works; production requires real `ServiceKey` and company NIF
  - QR dialog shown to user after successful submission

---

## Open Non-Blocking Issues

| Issue | File | Notes |
|-------|------|-------|
| Hardcoded DB path | `src/sql_lite/sql_lite.h` (`DB_PATH`) | Must be changed for each new machine |
| Hardcoded icon path | `src/app/main.cpp` | Same — points to specific user's OneDrive |
| Hardcoded report output path | `src/contabilidad/`, `src/Listado/genlistado.cpp` | Same issue |
| ServiceKey in plaintext INI | `src/verifactu/verifactuconfig.h` | Consider QSettings encryption |
| No retry for failed Verifactu calls | `src/verifactu/` | Planned |
| No Verifactu configuration UI | `src/verifactu/` | Planned |

---

## Completed Milestones

### Documentation expansion — May 2026
- [x] Created per-module reference docs for all non-Verifactu modules (`docs/modules/*.md`)
  - `mainwindow.md`, `sql_lite.md`, `listado.md`, `recog_prendas.md`, `facturas.md`, `contabilidad.md`, `imprimir.md`, `add_garment.md`
- [x] Condensed Verifactu docs: `README.md` (Spanish→English, shorter), `RESUMEN_IMPLEMENTACION.md` (246→85 lines), removed redundant `INDEX.md`
- [x] Updated `architecture.md` and `AI_agent_instructions.md` — method names reflect current camelCase identifiers
- [x] Removed `docs/todo/` and `docs/development/` references from all docs; deleted `hardcoded_paths.png`
- [x] Updated `docs/INDEX.md` and `docs/README.md` to register all new module docs

### Code quality improvements — May 2026 (`feature/add_mdiago_verifactu`)
- [x] Applied coding guidelines to all `src/` modules (commit `157c354`)
  - Translated Spanish dev comments and debug strings to English
  - Converted `SIGNAL`/`SLOT` macro syntax to new-style pointer syntax
  - Fixed SQL injection in `add_garment.cpp` `on_pb_search_pressed` (parameterised query)
- [x] Renamed all user-defined snake_case identifiers to camelCase across all `src/` modules (commit `551b920`)
  - Methods, members, parameters, local variables, free functions, signals
  - Qt auto-connect slots (`on_*`) and Qt virtual overrides preserved unchanged
  - Updated call sites in `insertnewitem.cpp` and `genlistado.cpp`

### Documentation reorganisation — May 2026
- [x] Moved all `.md` docs from `src/verifactu/` to `docs/modules/verifactu/`
- [x] Created `docs/README.md` (docs folder index)
- [x] Created `docs/modules/verifactu/` with full Verifactu docs
- [x] Created `docs/AI_agent_instructions.md` — agent onboarding guide
- [x] Created `docs/architecture.md` — full module/DB/flow documentation
- [x] Created `docs/progress_tracker.md` — this file
- [x] Created `docs/INDEX.md` — project-wide quick-find index
- [x] Created `.claude/commands/update-docs.md` and `.claude/commands/update-skills.md` skills
- [x] Created `.claude/commands/coding-guidelines.md` skill (`/coding-guidelines`)
- [x] All documentation translated to English

### v8.0 — Verifactu Support — May 2026 (`feature/add_mdiago_verifactu`)
- [x] Created `src/verifactu/` module: `VerifactuConfig`, `VerifactuInvoice`, `VerifactuManager`, `VerifactuIntegration`
- [x] Added `VerifactuIntegration` instance to `MainWindow` (`m_verifactuIntegration`)
- [x] `verifactuSubmitInvoice()` called in ticket save flow
- [x] `showQrToClient()` — QR + CSV + validation URL dialog
- [x] Graceful degradation: shows warning if Verifactu not configured, does not block ticket saving
- [x] TESTING environment available; production environment ready
- [x] CMake integration (`src/verifactu/CMakeLists.txt` + root `CMakeLists.txt`)
- [x] `initializeVerifactu()` in `MainWindow` constructor

### v7.1 — PDF export for listados
- [x] Print listado de prendas to PDF via `actionGenerar_pdf_con_el_listado`

### v7.0 — Hardcoded path update
- [x] Updated hardcoded paths for new laptop (still hardcoded, different machine address)

### v6.1 — Fix waiting cursor
- [x] Fixed waiting cursor in listados and other time-consuming operations

### v6.0 — Garment management upgrades
- [x] `AddGarment` module: add garments to existing tickets
- [x] Split garments in `RecogPrendas`
- [x] `hash` column added to `ingresos` for unique row identification
- [x] `RecogPrendas` DB updates based on `n_recibo` + `hash` (not just row number)

---

## Archive (Entries older than 6 months / no longer actionable)

### v5.x — Listado & accounting improvements
- [x] Revert accountings feature (v5.0)
- [x] Data lock on closed quarters (v5.6)
- [x] Generic `Listado` class replacing per-table views (v5.1+)
- [x] Sort/filter for all table columns
- [x] New `InsertNewItem` dialog for adding clients
- [x] Quarter lock prevents out-of-period data entry (v5.0)

### v4.x — Print, UI, and invoice improvements
- [x] Print support via Excel + batch script (v3.0/v4.x)
- [x] Debug mode toggle (v4.0)
- [x] Invoice form: show IVA and base, replace "producto" with description (v4.0)
- [x] App icon (v4.1)
- [x] Version metadata in `.exe` (v5.4)

### v1.0–v2.0 — Foundation
- [x] Initial release with garment/client/receipt/accounting/invoice features
- [x] SQLite database
- [x] `servicios` table for unique services in invoices
- [x] 2-decimal currency display
