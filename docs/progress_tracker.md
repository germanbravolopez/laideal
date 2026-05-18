# Progress Tracker — La Ideal

**Rule**: Add new entries at the **top** of the relevant section. When entries exceed ~6 months old and are no longer actionable, move them to the Archive at the bottom. Keep the most important and recent information near the top.

---

## Current Status — May 2026

**Active branch**: `feature/add_mdiago_verifactu`
**Version**: 8.0 (unreleased — blocked by items below)

---

## Blocking Issues (must fix before merging `feature/add_mdiago_verifactu`)

- [ ] **Remove temp debug code** in `src/app/mainwindow.cpp:23–27`
  - Constructor calls `verifactu_submit_invoice()` then `std::exit(0)`
  - App exits immediately on launch — cannot be used in this state
- [ ] **Persist Verifactu CSV to database** — the CSV received from AEAT after successful submission is logged via `qDebug()` but not saved anywhere
  - Needs `ALTER TABLE` to add columns (SQL in `docs/modules/verifactu/RESUMEN_IMPLEMENTACION.md`)
  - Then update `save_ticket()` or `verifactu_submit_invoice()` to write the CSV
- [ ] **Add Verifactu columns to DB schema** — `ingresos` (or `facturas`) needs `verifactu_csv`, `verifactu_timestamp`, `verifactu_estado`, `verifactu_error`, `verifactu_url_qr`

---

## In Progress

- [ ] Verifactu integration — code complete, pending production testing
  - Classes: `VerifactuIntegration`, `VerifactuManager`, `VerifactuConfig`, `VerifactuInvoice`
  - TESTING environment works; production requires real `ServiceKey` and company NIF
  - QR dialog shown to user after successful submission
- [ ] Documentation migration and expansion (this session, May 2026)
  - Moved verifactu docs from `src/verifactu/` to `docs/modules/verifactu/`
  - Created `docs/AI_agent_instructions.md`, `docs/architecture.md`, `docs/progress_tracker.md`, `docs/INDEX.md`
  - Created `.claude/commands/update-docs.md`, `.claude/commands/update-skills.md`

---

## Open Non-Blocking Issues

| Issue | File | Notes |
|-------|------|-------|
| Hardcoded DB path | `src/sql_lite/sql_lite.h:9` | Must be changed for each new machine |
| Hardcoded icon path | `src/app/main.cpp:12–13` | Same — points to specific user's OneDrive |
| ServiceKey in plaintext INI | `src/verifactu/verifactuconfig.h` | Consider QSettings encryption |
| No retry for failed Verifactu calls | `src/verifactu/` | Planned for v1.1 |
| No Verifactu configuration UI | `src/verifactu/` | Planned for v1.1 |

---

## Completed Milestones

### Documentation reorganisation — May 2026
- [x] Moved all `.md` docs from `src/verifactu/` to `docs/modules/verifactu/`
- [x] Created `docs/README.md` (docs folder index)
- [x] Created `docs/modules/verifactu/` with full Verifactu docs (5 files)
- [x] Moved `docs/planning_verifactu.md` to `docs/development/planning_verifactu.md`
- [x] Created `docs/AI_agent_instructions.md` — agent onboarding guide
- [x] Created `docs/architecture.md` — full module/DB/flow documentation
- [x] Created `docs/progress_tracker.md` — this file
- [x] Created `docs/INDEX.md` — project-wide quick-find index
- [x] Created `.claude/commands/update-docs.md` skill
- [x] Created `.claude/commands/update-skills.md` skill
- [x] All documentation translated to English

### v8.0 — Verifactu Support — May 2026 (`feature/add_mdiago_verifactu`)
- [x] Created `src/verifactu/` module: `VerifactuConfig`, `VerifactuInvoice`, `VerifactuManager`, `VerifactuIntegration`
- [x] Added `VerifactuIntegration` instance to `MainWindow`
- [x] `verifactu_submit_invoice()` called in ticket save flow
- [x] `show_qr_to_client()` — QR + CSV + validation URL dialog
- [x] Graceful degradation: shows warning if Verifactu not configured, does not block ticket saving
- [x] TESTING environment available; production environment ready
- [x] CMake integration (`src/verifactu/CMakeLists.txt` + root `CMakeLists.txt`)
- [x] `initialize_verifactu()` in `MainWindow` constructor

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
