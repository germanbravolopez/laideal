# Progress Tracker — La Ideal

**Rule**: Add new entries at the **top** of the relevant section. When entries exceed ~6 months old and are no longer actionable, move them to the Archive at the bottom. Keep the most important and recent information near the top.

---

## Current Status — May 2026

**Active branch**: `feature/add_mdiago_verifactu`
**Version**: 8.0 (ready to release — ships with TESTING environment; switch to PRODUCTION after meeting with IreneSolutions)

---

## Blocking Issues (must fix before merging `feature/add_mdiago_verifactu`)

- [x] **Contabilidad correctness with Verifactu** — fixed three bugs in accounting reports (`src/contabilidad/`, `src/sql_lite/`):
  1. **Syntax error**: `on_cb_config_currentTextChanged` in `contabilidad.cpp` was missing a closing `}` for the `else` block — prevented compilation.
  2. **ANULADA in quarterly totals**: `totalPriceBetweenDates` now excludes rows where `verifactu_estado = 'ANULADA'`; pre-v8.0 rows (NULL/empty estado) are still included. Correct per AEAT: a cancelled Verifactu invoice must not contribute to taxable income.
  3. **`gastos` double-count on quarter boundary**: `BETWEEN startDate AND endDate` was endpoint-inclusive, so an expense on the first day of the next quarter was counted in both quarters. Replaced with `>= startDate AND < endDate` — same half-open interval as `ingresos`.

Previously resolved blockers:
- [x] Temp debug code removed from `MainWindow` constructor
- [x] Verifactu response persisted to DB (`verifactu_*` columns, `migrateDatabase()`, `saveTicket(VerifactuResult)`)
- [x] `ValidationUrl` and `QrCode` captured from `/Create` response in `processResponse()`

---

## In Progress

- [x] Verifactu integration — fully validated end-to-end with real company credentials
  - `submitSimplifiedInvoice()` confirmed working: F2 invoice, CSV received from AEAT, QR resolves correctly on AEAT portal
  - API returns `QrCode` (base64 BMP), `ValidationUrl`, and `QrCodeUrl` in the submit response — all captured and persisted
  - DB persistence complete: `verifactu_*` columns added to `ingresos` via `migrateDatabase()`
  - v8.0 ships with TESTING environment; switch to PRODUCTION requires meeting with IreneSolutions for production ServiceKey
- [x] QR on printed receipt — Verifactu QR embedded at the bottom of the Excel ticket via `QXlsx::insertImage`. Image not persisted in DB: save-flow path reuses the pixmap from the `/Create` response; reprint path calls `/GetQrCode` through `VerifactuIntegration::generateQR()` using ticket data from `ingresos`
- [x] Verifactu info in RecogPrendas — "Verifactu" button shows estado/CSV/timestamp/error/AEAT link per ticket (QR image not stored; accessible via AEAT link)
- [x] Invoice cancellation — `CancelInvoiceDialog` (Herramientas → Anular factura Verifactu…): search by ticket number, confirm details, call `VerifactuIntegration::cancelInvoice()`, update DB (`verifactu_estado = 'ANULADA'`); confirmed working in TESTING environment
- [x] Unsuccessful invoice handling — define behaviour when Verifactu fails at save time: retry strategy, user notification, fallback storage
- [x] Logging mechanism — `src/logging/AppLogger` installed in `main.cpp`; all `qDebug`/`qWarning`/`qCritical` output written to `~/.laideal.log` with timestamps; rotates to `.laideal.log.old` at 5 MB; Herramientas → Log de depuración… shows path and opens folder in Explorer

---

## Open Non-Blocking Issues

| Issue | File | Notes |
|-------|------|-------|
| Improve report visualisation | `src/contabilidad/`, `src/listado/`, `src/imprimir/` | Improve the presentation of generated reports (contabilidad summaries, listados, printed receipts/invoices) — better formatting, layout, and readability. |
| Add sorting to table views | `src/listado/listado.cpp`, `src/recog_prendas/recog_prendas.cpp` | Allow users to sort table columns by clicking headers. `MySortFilterProxyModel` already supports `lessThan()`; wire up `QHeaderView::sectionClicked` and set `sortingEnabled` on each `QTableView`. |
| Switch Verifactu to PRODUCTION environment | `~/.laideal_settings.json`, `SettingsDialog` | v8.0 ships with TESTING environment. After meeting with IreneSolutions and obtaining production ServiceKey: update `verifactu.environment` and credentials in settings. No code change required — all handled via `SettingsDialog`. |
| Streamline build and deploy from CLI | `CMakeLists.txt`, root scripts | Add a single command (script or CMake target) to configure, build, and deploy the release `.exe` with all Qt dependencies — replacing the current manual Qt Creator / cmake-gui workflow. |
| GitHub Actions CI/CD pipeline | `.github/workflows/` | Set up a Windows runner that builds on every push/PR (CMake + MinGW), runs tests, and optionally produces a release artifact. Requires a self-hosted runner or a cross-compile setup since the app targets Windows. |
| Unit and integration tests | `tests/` | Add a test suite (Qt Test or Catch2) covering at minimum: `sql_lite` free functions, `VerifactuManager::processResponse()` response parsing, `MySortFilterProxyModel` diacritic filtering, and price calculation logic in `setGarmentPrice`. |
| Replace Excel-based printing with EPSON ticket printer API | `src/imprimir/imprimir.cpp` | Current receipt/invoice printing generates an Excel file and launches an external batch script. Should be replaced with direct EPSON ESC/POS (or equivalent) API calls to the ticket printer, removing the Excel template dependency entirely. |
| ServiceKey stored in plaintext JSON | `~/.laideal_settings.json` | Previously in INI, now in JSON — still plaintext; consider encryption |
| ~~No retry for failed Verifactu calls~~ | ~~`src/verifactu/`~~ | Done — see "Unsuccessful invoice handling" in In Progress |
| Clients missing from `Listado` table view but present in `MainWindow` combobox | `src/listado/listado.cpp`, `src/app/mainwindow.cpp` | Some clients appear in the combobox (populated via `readColumnFromTable`) but not in the list view. `RecogPrendas` search by name now uses diacritic-insensitive proxy filtering; investigate whether missing clients have encoding differences in the DB. |
| Price calculation for size-dependent garments (cortinas) may be incorrect | `src/app/mainwindow.cpp` (`setGarmentPrice`), `src/add_garment/add_garment.cpp` | When the size is not an exact value, verify that the price rounds or truncates correctly and matches what is shown on the receipt. |
| `RecogPrendas`: landline phone (`tel_fijo`) not shown | `src/recog_prendas/recog_prendas.h/cpp` | Add `tel_fijo` from `clientes` table alongside client name, so staff can call the client directly from the pickup panel. |
| ~~Establishment receipt copy: remove general conditions at the end~~ | ~~`src/imprimir/imprimir.cpp`~~ | Fixed — wrapped conditions block in `if (!isRecibo \|\| copyForClient)`. |
| ~~Client receipt: add RGPD data-protection clause~~ | ~~`src/imprimir/imprimir.cpp`~~ | **Done.** 4th bullet added to conditions block: controller = `businessName()`, purpose = gestión del servicio, rights = arts.15-22 RGPD, contact = `businessPhone()` (falls back to "ver cartel en tienda" if empty). Based on LOPDGDD Art. 11 (BOE-A-2018-16673), verified live against aepd.es. If a dedicated email is preferred over phone as the rights-contact, add `businessEmail()` to AppSettings and update the clause. |

---

## Completed Milestones

### Contabilidad dialog improvements — May 2026 (`feature/add_mdiago_verifactu`)
- [x] Contextual confirmation dialogs added to `on_bb_ok_cancel_accepted` (4 cases: lock/revert × already-done/not-done)
- [x] Fixed syntax bug: `QMessageBox::information` call had `, + "..."` as a separate argument instead of string concatenation
- [x] Fixed double-dialog: removed redundant `QMessageBox` calls from `updateLock()` (replaced with `qDebug`) since `on_bb_ok_cancel_accepted` now owns those dialogs
- [x] Fixed wrong comment: `checkBox_lock->setDisabled(revertirOn)` line had copy-paste "configuration combobox" comment — corrected to "lock checkbox when reverting"

### Herramientas menu reorganisation — May 2026 (`feature/add_mdiago_verifactu`)
- [x] `menuHerramientas` reordered: Imprimir, Recogida de prendas, Añadir nuevas prendas, ─, Añadir factura de gastos, Anular factura Verifactu, ─, Generar contabilidad, Revertir contabilidad
- [x] `actionFormulario_facturas` renamed to "Añadir factura de gastos"
- [x] Log de depuración moved from Herramientas to Archivo, below Configuración (before the separator)

### VerifactuEstado enum — May 2026 (`feature/add_mdiago_verifactu`)
- [x] `VerifactuEstado` enum class added to `verifactumanager.h` (NotSubmitted, Enviada, Anulada, Error)
- [x] `verifactuEstadoToString()` and `verifactuEstadoFromString()` inline helpers in the same header
- [x] All hardcoded `"ENVIADA"` / `"ANULADA"` / `"ERROR"` literals replaced in `mainwindow.cpp`, `cancelinvoicedialog.cpp`, `recog_prendas.cpp`
- [x] SQL literal `'ANULADA'` in `cancelinvoicedialog.cpp` replaced with parameterized bind

### Contabilidad bug fixes — May 2026 (`feature/add_mdiago_verifactu`)
- [x] Missing `}` in `contabilidad.cpp::on_cb_config_currentTextChanged` fixed (syntax error, prevented compilation)
- [x] `totalPriceBetweenDates` now excludes `verifactu_estado = 'ANULADA'` rows from ingresos — cancelled invoices no longer counted as taxable income
- [x] `gastos` query changed from `BETWEEN` (endpoint-inclusive) to `>= AND <` — eliminates double-counting on quarter boundary dates

### Unsuccessful invoice handling — May 2026 (`feature/add_mdiago_verifactu`)
- [x] `verifactuSubmitInvoice()` in `MainWindow` now shows `QMessageBox::warning` (Spanish) when submission fails with `ERROR` or `NETWORK_ERROR`; `INVALID_CONFIG` (not configured) remains silent
- [x] `retryVerifactuSubmit(ticketNum, invoiceDate)` added to `RecogPrendas`: sums ticket total from DB, resubmits via `submitSimplifiedInvoice`, updates all `n_recibo` rows in `ingresos`, refreshes table
- [x] `on_pb_verifactu_clicked()` extended: shows "Reintentar envío a AEAT" button in the info dialog when `estado == "ERROR"` and Verifactu is configured

### Persistent debug logging — May 2026 (`feature/add_mdiago_verifactu`)
- [x] New `src/logging/` module: `AppLogger` static class with `install()` and `logFilePath()`
- [x] `qInstallMessageHandler` captures all `qDebug` / `qWarning` / `qCritical` output — no changes needed to any existing call site
- [x] Log format: `yyyy-MM-dd HH:mm:ss [DEBUG|WARN|CRIT|FATAL] message`; session separator written on each app start
- [x] Rotation: file renamed to `.laideal.log.old` when it exceeds 5 MB; thread-safe via `QMutex`
- [x] Log file location: `~/.laideal.log` (consistent with `~/.laideal_settings.json`)
- [x] Herramientas → Log de depuración… dialog shows full path + "Abrir carpeta" button (opens Explorer)

### General conditions rewritten for legal correctness — May 2026 (`feature/add_mdiago_verifactu`)
- [x] Conditions block (client copy only) rewritten against RD 1453/1987 (BOE-A-1987-26716) and Consumo Responde (Junta de Andalucía)
- [x] Clause 1 (receipt/identity): receipt required to collect garment; loss does not prevent collection — client identifies themselves
- [x] Clause 2 (storage — legal fix): storage fees may accrue after 3 months from delivery; custody obligation ends at 6 months (Art. 6 RD 1453/1987 correctly applied to custody, not documentation)
- [x] Clause 3 (accessories — per-receipt): disclaimer for buttons/ornaments applies only when noted on *this specific receipt*; removal recommended before delivery
- [x] Clause 4 (NEW — pre-cleaning advisory, Art. 6 RD 1453/1987): if garment condition implies risk of damage or uncertain outcome, client will be informed before treatment proceeds

### RGPD first-layer notice added to client receipt — May 2026 (`feature/add_mdiago_verifactu`)
- [x] 4th bullet added to `createTicketExcel()` conditions block (client copy only): "Sus datos son tratados por [businessName] para gestionar el servicio (RGPD). Derechos arts.15-22: [businessPhone or 'ver cartel en tienda']." Complies with LOPDGDD Art. 11 layered-notice requirement verified live against aepd.es

### General conditions omitted from establishment receipt copy — May 2026 (`feature/add_mdiago_verifactu`)
- [x] `createTicketExcel()` conditions block wrapped in `if (!isRecibo || copyForClient)` — establishment copy now ends at the timestamp; invoice copies (no `isRecibo`) always show conditions as before

### Ingresos Listado: clickable Verifactu URL column — May 2026 (`feature/add_mdiago_verifactu`)
- [x] New `LinkDelegate` added to `src/tableview/` — renders non-empty cells as blue underlined text; follows the same `initStyleOption` pattern as `TextColorDelegate`
- [x] `INGRESOS_IDX_VERIFACTU_ERROR` (18) and `INGRESOS_IDX_VERIFACTU_URL_QR` (19) constants added to `mysortfilterproxymodel.h`
- [x] `Listado::populateTable()` applies `LinkDelegate` to the URL QR column and moves it visually before `verifactu_error` via `QHeaderView::moveSection()`
- [x] `Listado` constructor connects `table_listado->clicked` to open the URL in the system browser via `QDesktopServices::openUrl()`

### RecogPrendas bug fixes — May 2026 (`feature/add_mdiago_verifactu`)
- [x] Buttons (payment, state, pay-all, pku-all, separ-garm, print) no longer grey out after an action — moved `setEnabled(true)` calls into `updateRowClickedToFields()` so they fire after every `updateDb()` cycle, not only on direct row click
- [x] Print button in RecogPrendas now generates QR code — `m_verifactuIntegration` added as public member to `RecogPrendas`; `MainWindow` passes it at creation; `on_pb_print_clicked()` forwards it to `Imprimir`

### Ticket header + business phone setting — May 2026 (`feature/add_mdiago_verifactu`)
- [x] `createTicketExcel()` header rewritten: 6-row block (business name at font 22, then company name / NIF / address / city / phone at font 11 bold) sourced entirely from `AppSettings` — removed dead `formatCenterAlign` and broken `verifactuIntegration` call
- [x] `AppSettings::businessPhone()` / `setBusinessPhone()` added — persisted under `business.phone` in `~/.laideal_settings.json`
- [x] `SettingsDialog` Business tab gains "Teléfono:" field wired to `businessPhone`

### Verifactu QR on printed ticket — May 2026 (`feature/add_mdiago_verifactu`)
- [x] `Imprimir` gains `qrCode` (QPixmap) and `verifactuIntegration` members; `resolveQrCode()` returns the pre-supplied pixmap if any, otherwise calls `/GetQrCode` via `VerifactuIntegration::generateQR()` using ticket number/date/taxBase/taxRate from `ingresos`
- [x] `Imprimir::createTicketExcel()` embeds the QR (scaled 140×140) at the bottom of the receipt using `QXlsx::Document::insertImage()`
- [x] `Imprimir` column constants extended (`TABLE_VERIFACTU_CSV` … `TABLE_VERIFACTU_URL_QR`) for the new `ingresos` columns
- [x] `MainWindow::printRecibo()` / `printFra()` now take `const VerifactuResult &` and pass `result.qrCode` to `Imprimir` (no extra REST call when QR is already in hand)
- [x] `Imprimir → Recibo / Factura / Factura completa` reprint actions inject `m_verifactuIntegration` so the dialog can hit `/GetQrCode` for tickets without an in-memory pixmap
- [x] `VerifactuIntegration::generateQR()` signature fixed: now builds a valid F2 simplified invoice (with tax item) so the `/GetQrCode` endpoint accepts the request (previous version failed `isValid()` due to missing TaxItems)
- [x] `imprimir` CMake target now links the `verifactu` library

### Invoice cancellation — May 2026 (`feature/add_mdiago_verifactu`)
- [x] `CancelInvoiceDialog` added to `src/app/` — search ticket by number, show cliente/fecha/importe/CSV/estado, enable cancel only when `estado == "ENVIADA"`
- [x] `Herramientas → Anular factura Verifactu…` menu action opens the dialog; disabled with warning if Verifactu not configured
- [x] On AEAT confirmation: updates `ingresos` rows (`verifactu_estado = 'ANULADA'`, `verifactu_timestamp = now`, clears `verifactu_error`)
- [x] `VerifactuManager::cancelInvoice()` fixed to include `CompanyName` in cancel JSON — required by AEAT API
- [x] AEAT error 3002 "No existe el registro de facturación" diagnosed: TESTING environment purges old records; must cancel a freshly-submitted invoice in the same session

### RecogPrendas UX + Listado fixes — May 2026 (`feature/add_mdiago_verifactu`)
- [x] `showQrToClient()` removed from `MainWindow` — QR dialog after invoice submit was disruptive; Verifactu info is now accessible in `RecogPrendas` instead
- [x] `pb_verifactu` button added to `RecogPrendas` — enabled only when selected row has `verifactu_estado` non-empty; opens dialog showing estado, CSV, timestamp, error, and clickable AEAT validation link
- [x] All action buttons in `RecogPrendas` (`pb_payment`, `pb_state`, `pb_pay_all`, `pb_pku_all`, `pb_separ_garm`, `pb_print`, `pb_verifactu`) start disabled; enabled on row selection
- [x] `ingresos` Listado view now loads most-recent rows first (`ORDER BY n_recibo DESC`) so latest tickets are visible without loading the full table
- [x] Other tables in Listado use `model->fetchMore()` loop replacing the unreliable scrollbar hack
- [x] Row height no longer expands on lazy-load: `setDefaultSectionSize(rowHeight(0))` locks compact height after initial `resizeRowsToContents()`
- [x] Listado window width capped at `screen()->availableGeometry().width()` — prevents window from expanding off-screen when `ingresos` has many columns
- [x] `TABLE_VERIFACTU_CSV/TIMESTAMP/ESTADO/ERROR/URL_QR` (15–19) added to `recog_prendas.h`; hash + verifactu columns hidden from table view via `setColumnHidden()`

### Debug mode replaced by AppSettings::enablePrinting() — May 2026 (`feature/add_mdiago_verifactu`)
- [x] `bool debug` member and `on_actionModo_debug_triggered` slot removed from `MainWindow`
- [x] `actionModo_debug` UI action removed from `mainwindow.ui` (Archivo menu)
- [x] `AppSettings::enablePrinting()` / `setEnablePrinting()` added — persisted under `print.enable` in `~/.laideal_settings.json`
- [x] `SettingsDialog` General tab gains checkbox "Habilitar impresión de tickets y facturas"
- [x] `on_bb_save_reset_clicked()` now guards printing with `AppSettings::instance()->enablePrinting()` instead of `!debug`

### Verifactu DB persistence + response capture — May 2026 (`feature/add_mdiago_verifactu`)
- [x] `migrateDatabase()` added to `sql_lite` — adds 5 `verifactu_*` columns to `ingresos` on startup (idempotent)
- [x] `processResponse()` in `VerifactuManager` now extracts `ValidationUrl` and `QrCode` from the `/Create` response alongside `CSV`
- [x] `saveTicket()` extended to persist `verifactu_csv`, `verifactu_timestamp`, `verifactu_estado`, `verifactu_error`, `verifactu_url_qr` for every garment row in the ticket
- [x] `verifactuSubmitInvoice()` now returns `VerifactuResult`; caller in `on_bb_save_reset_clicked()` passes it to `saveTicket()`
- [x] Error and unconfigured states also written to DB (empty estado = not submitted; estado=ERROR stores error description)

### Module consolidation + tilde search fix — May 2026 (`feature/add_mdiago_verifactu`)
- [x] All src subdirectory names lowercased; root `CMakeLists.txt` updated to match
- [x] Five separate CMake libraries (`FilterWidget`, `MySortFilterProxyModel`, `NumberFormatDelegate`, `TextColorDelegate`, `TableView`) merged into a single `tableview` library — source files stay in their own dirs, all exposed via the `tableview` INTERFACE include path
- [x] `listado` library renamed to lowercase, now links `tableview` as PUBLIC (propagates headers to consumers)
- [x] `recog_prendas` and `app` `target_link_libraries` updated — no more references to old individual targets
- [x] `MySortFilterProxyModel::removeDiacritics()` added — strips Unicode combining marks via NFD decomposition; covers all Spanish accented characters and ñ
- [x] `MySortFilterProxyModel::setNormalizedFilter(text, column)` added — OR-combined with existing regex filter inside `filterAcceptsRow`; `column = -1` searches all columns
- [x] `Listado::textFilterChanged` now calls `setNormalizedFilter` so searches without tildes (e.g., "garcia") match names with tildes (e.g., "García")
- [x] `RecogPrendas` name search changed from SQL `WHERE cliente LIKE` (ASCII-only) to full `SELECT * FROM ingresos` + proxy-model normalized filter on `TABLE_CLIENT` column
- [x] `RecogPrendas` total-price calculation now sums from the proxy model rows (correct for name search filtered set)

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
