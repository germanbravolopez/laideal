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
- [ ] **Persist Verifactu response to database** — CSV, timestamp, estado, error, validation URL received from AEAT but not saved anywhere
  - `ALTER TABLE ingresos ADD COLUMN verifactu_csv TEXT`
  - `ALTER TABLE ingresos ADD COLUMN verifactu_timestamp TEXT`
  - `ALTER TABLE ingresos ADD COLUMN verifactu_estado TEXT`
  - `ALTER TABLE ingresos ADD COLUMN verifactu_error TEXT`
  - `ALTER TABLE ingresos ADD COLUMN verifactu_url_qr TEXT`
  - Then update `saveTicket()` to write the result after `verifactuSubmitInvoice()`
- [ ] **Capture `ValidationUrl` and `QrCode` from `submitInvoice()` response** — the API already returns both in the `Return` object but `processResponse()` only extracts `CSV`; update it to also capture `ValidationUrl` and `QrCode` so the QR dialog works without a separate QR generation call

---

## In Progress

- [ ] Verifactu integration — TESTING confirmed, pending DB persistence and production testing
  - `submitSimplifiedInvoice()` confirmed working: F2 invoice, CSV received from AEAT
  - API also returns `QrCode` (base64 BMP), `ValidationUrl`, and `QrCodeUrl` in the submit response
  - TESTING environment confirmed; production requires real `ServiceKey` and company NIF from `~/.laideal_cfg`
- [ ] QR on printed receipt — add Verifactu QR image to the Excel receipt layout (`src/imprimir/imprimir.cpp`)
- [ ] QR view in RecogPrendas — show stored CSV, QR image, and AEAT validation link per ticket in `src/recog_prendas/recog_prendas.h/cpp`
- [ ] Invoice cancellation — test cancellation flow via `cancelInvoice()` / `VerifactuManager::cancelInvoice()`; verify the rectified invoice CSV is returned and stored
- [ ] Unsuccessful invoice handling — define behaviour when Verifactu fails at save time: retry strategy, user notification, fallback storage

---

## Open Non-Blocking Issues

| Issue | File | Notes |
|-------|------|-------|
| ServiceKey stored in plaintext JSON | `~/.laideal_settings.json` | Previously in INI, now in JSON — still plaintext; consider encryption |
| No retry for failed Verifactu calls | `src/verifactu/` | Planned |
| Search fails for names with Spanish accents (tildes) | `src/sql_lite/sql_lite.cpp`, `src/recog_prendas/` | `selectFromWhereLike` and related search functions may not match names containing á, é, í, ó, ú, ñ stored in the DB. Audit all search paths and fix collation or normalization. Likely related to the issue below. |
| Clients missing from `Listado` table view but present in `MainWindow` combobox | `src/Listado/listado.cpp`, `src/app/mainwindow.cpp` | Some clients appear in the combobox (populated via `readColumnFromTable`) but not in the list view. Also: `RecogPrendas` search by name does not find them, but search by ticket number does. Investigate query differences — likely a collation or encoding mismatch, possibly the same root cause as the tilde issue. |
| Price calculation for size-dependent garments (cortinas) may be incorrect | `src/app/mainwindow.cpp` (`setGarmentPrice`), `src/add_garment/add_garment.cpp` | When the size is not an exact value, verify that the price rounds or truncates correctly and matches what is shown on the receipt. |
| `RecogPrendas`: landline phone (`tel_fijo`) not shown | `src/recog_prendas/recog_prendas.h/cpp` | Add `tel_fijo` from `clientes` table alongside client name, so staff can call the client directly from the pickup panel. |
| Establishment receipt copy: remove general conditions at the end | `src/imprimir/imprimir.cpp` | The copy printed for the shop should not include the general conditions block. Only the client copy needs it. |
| Client receipt: replace general conditions with data-protection notice | `src/imprimir/imprimir.cpp` | Reduce the general conditions text and add a GDPR/data-protection legend required by Spanish law. |

---

## Completed Milestones

### Centralised AppSettings + sql_lite refactor — May 2026 (`feature/add_mdiago_verifactu`)
- [x] New `src/appsettings/` module: `AppSettings` singleton + `SettingsDialog`
  - All previously hardcoded paths and values (DB path, icon, report dirs, Excel template, batch script, business identity, IVA rate, Verifactu NIF/name/key/environment) now live in `~/.laideal_settings.json`
  - Legacy `~/.laideal_cfg` and `~/.verifactu_key` files migrated automatically on first run
  - Settings dialog accessible from Archivo → Configuración...; changes applied at runtime (icon, Verifactu) or require restart (DB path)
- [x] `main.cpp` — validates DB path before constructing `MainWindow`; opens `SettingsDialog` on first run or misconfiguration
- [x] `sql_lite` refactored — `const QString &` params, `dbNotConfigured()` guard prevents error dialogs when DB path not set, `monthStr()` helper deduplicates month-padding logic, `QRandomGenerator` replaces `rand()` in `genHash16()`
- [x] All module CMakeLists.txt updated to link `appsettings`

### First successful Verifactu submission — May 2026 (`feature/add_mdiago_verifactu`)
- [x] `submitSimplifiedInvoice()` added to `VerifactuIntegration` for F2 (simplified) invoices — no buyer NIF required, correct for laundry retail
- [x] Fixed `CompanyName` field name — API requires `CompanyName`, was incorrectly sent as `SellerName`
- [x] Emitter NIF and name moved out of source code to `~/.laideal_cfg` (key=value, same location as `~/.verifactu_key`)
- [x] `logResponse()` added to `VerifactuManager` — pretty-prints server JSON with QR blob truncated
- [x] First confirmed AEAT TESTING submission: invoice `24417`, CSV `A-9VARYQTZTARVU2`, `StatusResponse: Correcto`
- [x] Confirmed API returns `QrCode`, `ValidationUrl`, and `QrCodeUrl` directly in the `submitInvoice` response

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

### 8.0 — Verifactu Support — May 2026 (`feature/add_mdiago_verifactu`)
- [x] Created `src/verifactu/` module: `VerifactuConfig`, `VerifactuInvoice`, `VerifactuManager`, `VerifactuIntegration`
- [x] Added `VerifactuIntegration` instance to `MainWindow` (`m_verifactuIntegration`)
- [x] `verifactuSubmitInvoice()` called in ticket save flow
- [x] `showQrToClient()` — QR + CSV + validation URL dialog
- [x] Graceful degradation: shows warning if Verifactu not configured, does not block ticket saving
- [x] TESTING environment available; production environment ready
- [x] CMake integration (`src/verifactu/CMakeLists.txt` + root `CMakeLists.txt`)
- [x] `initializeVerifactu()` in `MainWindow` constructor

### r7.1 — PDF export for listados
- [x] Print listado de prendas to PDF via `actionGenerar_pdf_con_el_listado`

### r7.0 — Hardcoded path update
- [x] Updated hardcoded paths for new laptop (still hardcoded, different machine address)

### r6.1 — Fix waiting cursor
- [x] Fixed waiting cursor in listados and other time-consuming operations

### r6.0 — Garment management upgrades
- [x] `AddGarment` module: add garments to existing tickets
- [x] Split garments in `RecogPrendas`
- [x] `hash` column added to `ingresos` for unique row identification
- [x] `RecogPrendas` DB updates based on `n_recibo` + `hash` (not just row number)

---

## Archive (Entries older than 6 months / no longer actionable)

### r5.x — Listado & accounting improvements
- [x] Revert accountings feature (r5.0)
- [x] Data lock on closed quarters (r5.6)
- [x] Generic `Listado` class replacing per-table views (r5.1+)
- [x] Sort/filter for all table columns
- [x] New `InsertNewItem` dialog for adding clients
- [x] Quarter lock prevents out-of-period data entry (r5.0)

### r4.x — Print, UI, and invoice improvements
- [x] Print support via Excel + batch script (r3.0/r4.x)
- [x] Debug mode toggle (r4.0)
- [x] Invoice form: show IVA and base, replace "producto" with description (r4.0)
- [x] App icon (r4.1)
- [x] Version metadata in `.exe` (r5.4)

### r1.0–r2.0 — Foundation
- [x] Initial release with garment/client/receipt/accounting/invoice features
- [x] SQLite database
- [x] `servicios` table for unique services in invoices
- [x] 2-decimal currency display
