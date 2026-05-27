# Progress Tracker — La Ideal

**Rule**: Track only what is actionable now. The active sections are:
- **Blocking Issues** — must-fix items before merging the current branch and cutting a release. Items move out of here once resolved (into Completed Milestones below).
- **Open Non-Blocking Issues** — known backlog that does not gate the release.
- **Completed Milestones** — finished work, newest at the top. Provides history for the changelog.
- **Archive** — entries older than ~6 months and no longer actionable.

Add new entries at the **top** of the relevant section. Do not keep an "In Progress" list — work in progress lives in the branch and in Blocking Issues if it gates the release.

---

## Current Status — May 2026

**Active branch**: `feature/add_mdiago_verifactu`
**Version**: 8.0 (ready to release — ships with TESTING environment; switch to PRODUCTION after meeting with IreneSolutions)

---

## Blocking Issues (must fix before merging `feature/add_mdiago_verifactu`)

> Verifactu legal-compliance gaps below come from the audit in [`docs/modules/verifactu/verifactu-requirements.md`](modules/verifactu/verifactu-requirements.md) (RD 1007/2023 + Orden HAC/1177/2024 + AEAT guidance). Each one corresponds to a numbered requirement in that file.

_(All known Verifactu compliance gaps closed.)_

---

## Open Non-Blocking Issues

| Issue | File | Notes |
|-------|------|-------|
| Verifactu Req. 4 — automated SQLite backup + 4-year retention (Art. 8.2.c RD 1007/2023) | `src/sql_lite/`, deploy scripts | The regulation requires a "procedimiento de descarga, volcado y archivo seguro" — durable storage during the tax prescription period (4 años, longer in some cases). Today the DB lives at the hardcoded path on a single machine with no automated backup, no offsite copy, no retention policy: a disk failure loses everything. Minimum viable fix: a nightly scheduled task (Windows Task Scheduler entry shipped with the installer) that copies `<dbPath>` to a timestamped `.sqlite` under a configurable backup root, with rotation that keeps daily for 30 days + monthly for 4 years. Stretch: add an in-app `Herramientas → Hacer copia de seguridad ahora...` action for manual triggers, and verify backup integrity by opening the copy read-only. Tracked here from `docs/modules/verifactu/verifactu-requirements.md` Req. 4 PARTIAL. |
| Verifactu Req. 5 — per-user identification (Art. 8.1 RD 1007/2023, trazabilidad) | `src/app/main.cpp`, `src/app/mainwindow.cpp` | The regulation requires each invoicing action to be traceable to a specific operator. La Ideal is single-user today (no login, no session, no per-operator audit field on `ingresos`). For the current single-operator deployment this is implicitly satisfied — but the moment two clerks share the same Windows account, traceability is lost. Minimum fix for multi-operator scenarios: prompt for an operator code at app start, persist it in memory, and stamp it on each `ingresos` write (new column `operador TEXT`). The `declaración responsable` could explicitly state single-operator scope to formally close the requirement under the current deployment. Tracked here from `docs/modules/verifactu/verifactu-requirements.md` Req. 5 PARTIAL. |
| Coding-guidelines Tier 1 cleanups (6 items, all Tier-1 "real discrepancies") | various | Snapshot from `docs/coding_guidelines_audit.md` 2026-05-26 audit. (1) [src/tableview/tableview.cpp:14-15](../src/tableview/tableview.cpp#L14-L15) — last surviving old-style `SIGNAL(...)` / `SLOT(...)` macro; convert to `&TableView::customContextMenuRequested` pointer syntax. (2) [src/imprimir/imprimir.cpp:395](../src/imprimir/imprimir.cpp#L395) — local `QString estado` (Spanish identifier in recently-written code); rename to `state` / `verifactuState`. (3) [src/app/cancelinvoicedialog.cpp:79-83](../src/app/cancelinvoicedialog.cpp#L79-L83) — locals `cliente`/`fecha`/`estado` in recently-written code; rename to `client`/`date`/`state`. (4) [src/recog_prendas/recog_prendas.cpp:595](../src/recog_prendas/recog_prendas.cpp#L595) — local `estado` in recently-written code; rename to `state`. (5) [src/verifactu/verifactuinvoice.h:10,60](../src/verifactu/verifactuinvoice.h#L10) — two classes (`VerifactuTaxItem` + `VerifactuInvoice`) in one header; split into `verifactutaxitem.h` + `verifactuinvoice.h` and update includes. (6) [src/listado/genlistado.h:40](../src/listado/genlistado.h#L40) — English typo `add_sufix_to_filename()`; rename to `add_suffix_to_filename()` (still snake_case — that module is grandfathered legacy). Tier 2 (architecturally justified) and Tier 3 (legacy/grandfathered) findings are not actionable and stay documented in the audit doc. |
| Dead-code cleanup in `src/verifactu/` (~20 unused methods, all in `VerifactuConfig` / `VerifactuInvoice` / `VerifactuTaxItem`) | `src/verifactu/verifactuconfig.h`, `src/verifactu/verifactuinvoice.h` | Full list in `docs/dead_code_report.md`. The async refactor and the removal of `createAndSubmitInvoice` promoted previously-transitively-dead members to first-class dead: in `VerifactuConfig` — `getEmitterCompanyName()`, `getSystemDeveloper()`, public `load()` (config is actually loaded via AppSettings); in `VerifactuTaxItem` — `setTaxType`, `setOperationType`, `setDescription`, `getTaxType`, `getTaxRate`, `getOperationType`; in `VerifactuInvoice` — `setBuyerNIF/Name`, `setTotalAmount`, `setTotalTaxAmount`, `getInvoiceType`, `getDescription`, `getBuyerNIF/Name`, `getTotalTaxAmount`, `clearTaxItems`, the member-overload `operationTypeToString` / `taxTypeToString`. Also unused: `MySortFilterProxyModel::filterMinimumDate/setFilterMinimumDate/filterMaximumDate/setFilterMaximumDate` (no `Listado` view wires them up). Safe to remove — codebase is self-contained (no external linkers). Re-run the `dead-code-finder` subagent before deleting to refresh the snapshot. |
| Drop legacy `VerifactuConfig` INI cache (`<AppDataLocation>/verifactu_config.ini`) | `src/verifactu/verifactuconfig.h/.cpp`, `src/verifactu/verifactumanager.cpp`, `src/verifactu/verifactuintegration.cpp` | `VerifactuConfig::save()` still writes a `QSettings` INI copy of NIF/name/serviceKey/environment to the local AppData folder on every save, but `loadEmitterConfiguration()` always overwrites those fields from `AppSettings` (the JSON in `~/.laideal_settings.json`) at startup — the INI is never read back. The INI is therefore a stale plaintext leak of the same credentials already stored elsewhere. Remove the QSettings write path from `save()`, drop the `load()` call from the constructor, and delete the AppData INI on first run as a cleanup migration. Per `docs/modules/verifactu/README.md` "Internal note". |
| Persist pending Verifactu submissions across app restarts | `src/verifactu/`, `src/app/mainwindow.cpp`, `src/recog_prendas/recog_prendas.cpp` | The async refactor leaves a durability gap: `m_pendingSubmits` (reqId → ticketNum) lives only in RAM, so if the app crashes or is closed mid-submission the row stays at `verifactu_estado = PENDIENTE` with no automatic recovery. Worst case: AEAT received the POST and registered the invoice but our reply was lost — a naive resubmit gets a duplicate-InvoiceID rejection. Fix: persist pending entries to a small file (or new `verifactu_pending` DB column) on insert/remove; on startup, for each surviving entry call an AEAT lookup endpoint (or `getQrCode`, which fails cleanly when the invoice is not registered) to determine real state — if registered, fetch CSV and mark Enviada; if not, resubmit. Requires confirming whether the Irene Solutions Kivu API has an idempotent lookup-by-InvoiceID endpoint. |
| Manual `hash` column — review necessity, fix duplicates | `src/sql_lite/sql_lite.h` (`genHash16`), `src/app/mainwindow.cpp` (`on_actionCrear_hash_en_ingresos_triggered`, `cleanDatabase`) | The 16-char ASCII hash on every `ingresos` row was added in r6.0 to disambiguate garment-level updates from `RecogPrendas`. Open questions: (a) is the hash still required given SQLite's intrinsic `rowid` could serve the same purpose? (b) the "Crear hash" maintenance action reports duplicates — investigate where they come from and decide a resolution policy (regenerate, merge, error out). If keeping hashes, replace `QRandomGenerator`-based `genHash16()` with `QUuid::createUuid().toString(QUuid::WithoutBraces)` (collision-free, no manual retry loop). Drop the "crear hash" maintenance action once the schema is solid. |
| Bundle icon with installer | `releases/laideal.iss`, `src/appsettings/appsettings.cpp`, `~/.laideal_settings.json`, `SettingsDialog` | Today the user must still set the `app.icon_path` in Configuración after install — it's an app-internal resource the user shouldn't touch. Ship it inside the installer (`Program Files/Laideal/resources/`) and have `AppSettings` resolve it via `QCoreApplication::applicationDirPath()` instead of a free-form path. Remove the corresponding field from `SettingsDialog`. Migration: on app start, if the JSON still points to a user path, ignore it and use the bundled default. (Note: the Excel template and print script were collapsed into hardcoded `~/.laideal_ticket.xlsx` / `~/.laideal_print.vbs` paths in a prior commit — only the icon remains.) |
| Per-payment Verifactu InvoiceID seq (restore per-garment payment) | `src/recog_prendas/`, `src/sql_lite/`, `src/verifactu/`, `src/app/cancelinvoicedialog.cpp` | v8.0 disables per-garment payment as a workaround for AEAT duplicate-InvoiceID rejection. To restore the UX, treat each payment event as a separate factura simplificada: add `verifactu_invoice_seq INTEGER DEFAULT 0` to `ingresos`; submit with `InvoiceID = "<n_recibo>-<seq>"` where `seq` increments per payment event for the same `n_recibo`; each submission carries only the amount paid in that event (sum of rows being paid now), not the full ticket total. `verifactu_csv` / `verifactu_url_qr` / `verifactu_estado` stay per-row (each row already corresponds to a garment paid in a specific event). `Contabilidad::totalPriceBetweenDates()` already sums per-row `importe`, so the multi-invoice case is transparent for accounting. `CancelInvoiceDialog` would need to cancel each `(n_recibo, seq)` pair separately. After landing, re-enable `pb_payment` in `updateRowClickedToFields()` / `on_tableView_clicked()`. |
| Improve report visualisation | `src/contabilidad/`, `src/listado/`, `src/imprimir/` | Improve the presentation of generated reports (contabilidad summaries, listados, printed receipts/invoices) — better formatting, layout, and readability. |
| Add sorting to table views | `src/listado/listado.cpp`, `src/recog_prendas/recog_prendas.cpp` | Allow users to sort table columns by clicking headers. `MySortFilterProxyModel` already supports `lessThan()`; wire up `QHeaderView::sectionClicked` and set `sortingEnabled` on each `QTableView`. |
| Switch Verifactu to PRODUCTION environment | `~/.laideal_settings.json`, `SettingsDialog` | v8.0 ships with TESTING environment. After meeting with IreneSolutions and obtaining production ServiceKey: update `verifactu.environment` and credentials in settings. No code change required — all handled via `SettingsDialog`. |
| Streamline build and deploy from CLI | `CMakeLists.txt`, root scripts | Add a single command (script or CMake target) to configure, build, and deploy the release `.exe` with all Qt dependencies — replacing the current manual Qt Creator / cmake-gui workflow. |
| GitHub Actions CI/CD pipeline | `.github/workflows/` | Set up a Windows runner that builds on every push/PR (CMake + MinGW), runs tests, and optionally produces a release artifact. Requires a self-hosted runner or a cross-compile setup since the app targets Windows. |
| Unit and integration tests | `tests/` | Add a test suite (Qt Test or Catch2) covering at minimum: `sql_lite` free functions, `VerifactuManager::processResponse()` response parsing, `MySortFilterProxyModel` diacritic filtering, and price calculation logic in `setGarmentPrice`. |
| Replace Excel-based printing with EPSON ticket printer API | `src/imprimir/imprimir.cpp` | Current receipt/invoice printing generates an Excel file and drives Excel COM via a generated VBScript (`cscript //nologo //B`) to send it to the default printer. Should be replaced with direct EPSON ESC/POS (or equivalent) API calls to the ticket printer, removing the Excel + cscript dependency entirely. |
| ServiceKey stored in plaintext JSON | `~/.laideal_settings.json` | Previously in INI, now in JSON — still plaintext; consider encryption |
| Clients missing from `Listado` table view but present in `MainWindow` combobox | `src/listado/listado.cpp`, `src/app/mainwindow.cpp` | Some clients appear in the combobox (populated via `readColumnFromTable`) but not in the list view. `RecogPrendas` search by name now uses diacritic-insensitive proxy filtering; investigate whether missing clients have encoding differences in the DB. |
| Price calculation for size-dependent garments (cortinas) may be incorrect | `src/app/mainwindow.cpp` (`setGarmentPrice`), `src/add_garment/add_garment.cpp` | When the size is not an exact value, verify that the price rounds or truncates correctly and matches what is shown on the receipt. |
| `RecogPrendas`: landline phone (`tel_fijo`) not shown | `src/recog_prendas/recog_prendas.h/cpp` | Add `tel_fijo` from `clientes` table alongside client name, so staff can call the client directly from the pickup panel. |

---

## Completed Milestones

### Pre-release bug fixes — May 2026 (`feature/add_mdiago_verifactu`)
- [x] **Verifactu — Factura rectificativa UI (R1-R5)** (Req. 1; Art. 8.2.a RD 1007/2023): new `RectifyInvoiceDialog` (modelled on `CancelInvoiceDialog`) under `Herramientas → Rectificar factura Verifactu...`. Operator searches by `n_recibo`, picks one of R1-R5 (R1 default - error fundado en derecho), picks **Por diferencias (I)** or **Por sustitución (S)**, enters either the corrected total (S) or the signed delta (I) including IVA, and submits asynchronously via the new `VerifactuIntegration::submitRectificationAsync()`. The submission builds an F2-style invoice (La Ideal only issues simplified) with `InvoiceType = R1..R5`, `RectificationType = S|I`, and - for S - `RectificationTaxBase`/`RectificationTaxAmount` carrying the original values being replaced. New columns `verifactu_rectifies_n_recibo` (TEXT, link to original) and `verifactu_rectification_type` (TEXT, "S"/"I") added via `migrateDatabase()`. A new `VerifactuEstado::Rectificada` (`"RECTIFICADA"`) marks the original rows when the rectificativa is submitted in substitution mode; `totalPriceBetweenDates()` now excludes RECTIFICADA the same way it excludes ANULADA, so the books reflect the corrected total without double-counting. For differences (I) the original rows stay ENVIADA and the delta row alone reconciles accounting. The new rectificativa row gets the next available `n_recibo`, `pagado = SI`, `estado = "Rectificativa"`, `prenda = "Rectificativa de ticket <orig>"`, and full Verifactu metadata (csv/xml/hash/url) from the AEAT reply. AEAT XML export (`Herramientas → Exportar registros AEAT (XML)...`) broadened to include ANY row with non-empty `verifactu_xml` regardless of estado (ENVIADA/ANULADA/RECTIFICADA) - all were sent to AEAT and Hacienda has them on record. Negative `VerifactuTaxItem::taxBase` is now accepted (was previously rejected by `isValid()`) because a downward-correction rectificativa por diferencias is a legitimate use case. `VerifactuInvoice::RECTIFICATION` enum value renamed to `RECTIFICATION_R1` and joined by `RECTIFICATION_R2..R5`; new `RectificationType` (BY_DIFFERENCES / BY_SUBSTITUTION) and `isRectificationInvoiceType()` helper. `isValid()` no longer demands a buyer NIF on rectificativas of simplified invoices. Files touched: `src/verifactu/verifactuinvoice.h/.cpp`, `src/verifactu/verifactumanager.h`, `src/verifactu/verifactuintegration.h/.cpp`, `src/sql_lite/sql_lite.cpp`, `src/app/rectifyinvoicedialog.h/.cpp` (new), `src/app/mainwindow.h/.cpp`, `src/app/mainwindow.ui`, `src/app/CMakeLists.txt`, `src/imprimir/imprimir.h`, `src/recog_prendas/recog_prendas.h/.cpp`, `src/tableview/mysortfilterproxymodel.h`. Audit table in `docs/modules/verifactu/verifactu-requirements.md` updated to COVERED.
- [x] **DB interactions: comprehensive qDebug/qWarning logging**: audit of every `q.prepare` / `q.exec` call site under `src/`. Every WRITE (INSERT / UPDATE / DELETE / ALTER) now has both (a) a `qDebug()` at the start of the operation showing the action and the key identifiers (ticket / hash / client / table / column / etc.) and (b) a `qWarning()` on `exec()` failure carrying `q.lastError().text()`. One real bug found and fixed alongside: `CancelInvoiceDialog::onVerifactuRequestFinished()` at the AEAT-cancel-confirmed branch called `q.exec()` on the `UPDATE ingresos SET verifactu_estado = ANULADA` statement without checking the return value — a silent failure would have left the row showing ENVIADA in the UI while the user had been told the cancellation was confirmed. SELECTs were not blanket-logged (each is read with `q.first()` / `q.next()` / `q.isSelect()` which already surface errors at the result-reading step; blanket-logging them would just be noise). Files touched: `src/sql_lite/sql_lite.cpp` (5 helpers), `src/recog_prendas/recog_prendas.cpp` (`updateDb()` start), `src/app/cancelinvoicedialog.cpp` (the silent UPDATE), `src/app/mainwindow.cpp` (per-row save log), `src/facturas/facturas.cpp` (`saveFactura`), `src/add_garment/add_garment.cpp` (`saveFactura`).
- [x] **Printed ticket columns too wide for printer — verified on the physical thermal printer**: column widths reduced from (5, 21, 8) = 34 units to (4, 20, 7.5) = 31.5 units in `Imprimir::createTicketExcel()`, and `excel.setPageMargins(0.2, 0.1, 0.1, 0.4)` added (left/right/top/bottom in inches; the asymmetric margins were tuned empirically against the actual thermal printer output by the user — top tighter than bottom, left slightly larger than right). The page-margin call uses the new `Worksheet::setPageMargins` API patched into the vendored QXlsx (see "QXlsx — page-margin support" milestone). Ticket now prints on a single page with no horizontal split. Files touched: `src/imprimir/imprimir.cpp`.
- [x] **Verifactu — fix `verifactu_xml` / `verifactu_hash` never being persisted from the pay-at-pickup path**: when the AEAT submission came from the `RecogPrendas → Pagar todo` flow (not the save-at-creation flow in `MainWindow`), the duplicate `RecogPrendas::updateTicketVerifactuFields()` had not been updated alongside `MainWindow::updateTicketVerifactuFields()` during the xml/hash work, so its prepared UPDATE statement was missing the `verifactu_xml = :xml` and `verifactu_hash = :hash` columns. Added both columns to the prepared statement and the corresponding `bindValue()` calls (with empty strings on the error branch, matching the MainWindow shape). Also added a `qDebug()` line at the top of both `updateTicketVerifactuFields()` implementations that logs ticket, estado, csv, hash, xml_len, error — so any future regression of this kind is visible in `~/.laideal.log` rather than silent. Worth flagging the underlying issue: the two implementations are near-duplicates and drift like this is the predictable outcome — a future refactor should fold them into a free function in `sql_lite.h` taking `db`, `ticketNum` and a `VerifactuResult` so there is one source of truth. Files touched: `src/app/mainwindow.cpp`, `src/recog_prendas/recog_prendas.cpp`.
- [x] **Verifactu — persist chained hash from AEAT response** (Req. 3; Art. 12 RD 1007/2023): the SHA-256 chained hash returned by AEAT/Irene as `<sum1:Huella>...</sum1:Huella>` inside `Return.Xml` is now captured into a new `VerifactuResult::rawHash` field by `VerifactuManager::processResponse()` (regex tolerant of any namespace prefix; normalised to upper-case hex) and persisted to a new `verifactu_hash TEXT` column on `ingresos` (added in `migrateDatabase()`). INSERT in `saveTicket()` writes empty; `updateTicketVerifactuFields()` writes the real value on Enviada or clears it on failure - same async path as csv/url/xml. Column constants `TABLE_VERIFACTU_HASH = 21` (imprimir.h, recog_prendas.h) and `INGRESOS_IDX_VERIFACTU_HASH = 21` (mysortfilterproxymodel.h); column hidden in `RecogPrendas::tableView` and `Listado` ingresos view. (Identifiers use the English term `hash`/`rawHash` per `/coding-guidelines`; the regulatory Spanish term "Huella" appears only in code comments and the XPath-style regex matching the AEAT XML element name.) Strengthens local tamper-detection independently of the AEAT side - the hash chains to the previous record's, so any silent edit/insert in `ingresos` is detectable by recomputing and comparing. Files touched: `src/sql_lite/sql_lite.cpp`, `src/verifactu/verifactumanager.h/.cpp`, `src/app/mainwindow.cpp`, `src/imprimir/imprimir.h`, `src/recog_prendas/recog_prendas.h/.cpp`, `src/listado/listado.cpp`, `src/tableview/mysortfilterproxymodel.h`. Audit table in `docs/modules/verifactu/verifactu-requirements.md` updated to COVERED.
- [x] **AppSettings — fewer user-facing paths**: collapsed five user-configurable paths into one. (1) `print.template_path` removed: the Excel ticket file is now always regenerated at the hardcoded `~/.laideal_ticket.xlsx` (matches the `~/.laideal.log` / `~/.laideal_settings.json` convention) — new `AppSettings::ticketExcelPath()` static helper. (2) `print.script_path` removed: `Imprimir::printTicket()` now generates a small VBScript at `~/.laideal_print.vbs` with the Excel-COM print logic templated with the current xlsx path, then invokes `cscript //nologo //B <path>.vbs` directly — no separate `.bat` to ship, no setting to configure. New `AppSettings::ticketPrintScriptPath()` static helper. (3) `reports.contabilidad_path` / `reports.listados_prendas_path` / `reports.listados_gastos_path` collapsed into a single `reports.root`: getters `contabilidadPath()`, `listadosPrendasPath()`, `listadosGastosPath()` now compose `<root>/Contabilidad`, `<root>/Listados/Prendas`, `<root>/Listados/Gastos` respectively — subdirs are hardcoded, mkpath-on-demand is unchanged at the call sites. Setters removed for all three (users only edit the root). Migration: `migrateLegacyKeys()` runs at JSON load and strips the well-known suffix from any of the three legacy values (`Contabilidad`, `Listados/Prendas`, `Listados/Gastos`) to derive the root; falls back to the parent dir of the contabilidad path; ultimate default is `~/Tintoreria`. Obsolete keys are removed via the new `removeNested()` helper. `SettingsDialog`'s "Rutas de salida" tab now shows one row instead of five, with a grey note listing the hardcoded subfolders. Files touched: `src/appsettings/appsettings.h/.cpp`, `src/appsettings/settingsdialog.h/.cpp`, `src/imprimir/imprimir.cpp`. Existing call sites in `contabilidad.cpp` / `genlistado.cpp` are untouched (the composed getters return the same shape they expect). The old `resources/print_ticket.bat` is no longer referenced by the app; left in the repo as a reference for the in-app VBScript.
- [x] **Verifactu — XML export of `ingresos` records for Hacienda** (Req. 7; Art. 14.1 RD 1007/2023, Orden HAC/1177/2024): the AEAT-style XML returned by Irene Solutions in `Return.Xml` of every `/Create` reply is now captured into a new `VerifactuResult::rawXml` field by `VerifactuManager::processResponse()` and persisted in a new `verifactu_xml TEXT` column on `ingresos` (added in `migrateDatabase()` so existing DBs auto-migrate). `MainWindow::saveTicket()` writes an empty placeholder on initial INSERT (estado=PENDIENTE) and `updateTicketVerifactuFields()` UPDATEs the column with the real XML when AEAT replies (estado=ENVIADA) or clears it on failure. A new menu action **Herramientas → Exportar registros AEAT (XML)...** opens a small inline dialog (two `QDateEdit` for the range), then `QFileDialog::getSaveFileName` for the output path, and writes a single envelope file `<RegistrosFacturacionLaIdeal>` with attributes `fechaDesde / fechaHasta / generadoEl / nif / emisor`, containing one `<Registro nRecibo=… fechaRecepcion=… importe=… csv=…>` child per qualifying row (estado=ENVIADA AND non-empty `verifactu_xml` AND `fecha_recepcion` in range). Each row's stored payload is inlined verbatim with its leading `<?xml ?>` declaration stripped (single regex) so the outer document stays well-formed. Caveat: only tickets submitted after this fix landed have `verifactu_xml` stored; older Enviada rows are silently skipped (no retroactive AEAT lookup — would need a separate REST endpoint, out of scope). Hidden in `RecogPrendas::tableView` and `Listado` to keep the column from blowing up row heights. Files touched: `src/sql_lite/sql_lite.cpp`, `src/verifactu/verifactumanager.h/.cpp`, `src/app/mainwindow.h/.cpp`, `src/app/mainwindow.ui`, `src/imprimir/imprimir.h`, `src/recog_prendas/recog_prendas.h/.cpp`, `src/listado/listado.cpp`, `src/tableview/mysortfilterproxymodel.h`. Audit table in `docs/modules/verifactu/verifactu-requirements.md` updated to COVERED.
- [x] **Verifactu — mandatory verification leyenda next to the QR on printed receipts** (Req. 9; Disposición Final Primera RD 1007/2023): `Imprimir::createTicketExcel()` now writes the centred line `Factura verificable en la sede electrónica de la AEAT` (font 7, merged A:C, row height 9.6) immediately below the 140×140 QR. Emitted only when the model's row-0 `verifactu_estado` equals `"ENVIADA"` (compared via the canonical `verifactuEstadoToString(VerifactuEstado::Enviada)` helper instead of a bare string literal), so PENDIENTE / ERROR / ANULADA rows never carry the verifiability claim — and save-time prints (which always have estado=PENDIENTE in the new async flow) silently skip it too. The full-sentence form is chosen over the shorter `VERI*FACTU` mark because the receipt has the vertical room and the long form directs the customer to the AEAT sede explicitly. Files touched: `src/imprimir/imprimir.cpp`. Audit table in `docs/modules/verifactu/verifactu-requirements.md` updated to COVERED.
- [x] **QXlsx — page-margin support added (local patch)**: upstream QXlsx (QtExcel/QXlsx, including the `liufeijin` fork bundled here) has the six `pageMargins` fields (`PMleft/PMright/PMtop/PMbotton/PMheader/PMfooter`) in `WorksheetPrivate` and serializes the `<pageMargins>` XML element when all six are non-empty, but exposes no public setter — the only public page method was `setStartPage`. Added `Worksheet::setPageMargins(left, right, top, bottom, header=0.3, footer=0.3)` in `QXlsx/header/xlsxworksheet.h` + `QXlsx/source/xlsxworksheet.cpp` (following the `liufeijin`/`setStartPage` pattern), and a `Document::setPageMargins(...)` forwarder in `QXlsx/header/xlsxdocument.h` + `QXlsx/source/xlsxdocument.cpp` that delegates to the current worksheet. Units are inches (XLSX standard). `imprimir.cpp` now calls `excel.setPageMargins(0.6, 0.6, 0.4, 0.4)` right after the column-width setup in `createTicketExcel()`. Note: the serializer is all-or-nothing — passing only some margins would still drop the whole element, hence the header/footer defaults (0.3″) so a 4-argument call always works. This is the first local modification to `QXlsx/` in this project; the AI agent docs previously said "do not modify" — that note has been relaxed to "minimal local patch — see Completed Milestones".
- [x] **Verifactu — declaración responsable visible in the software** (Req. 8; Art. 13 RD 1007/2023): new `Ayuda` menu with `Acerca de Verifactu...` action opens a `QDialog` displaying the fixed-text declaración. Producer NIF / name / address are pulled from `AppSettings` (the bespoke deployment treats the user-business as producer); software name is `La Ideal` and the version is interpolated from `PROJECT_VERSION_MAJOR/MINOR` in the generated `version.h`, so the declaration is automatically per-version as Art. 13 requires. Body cites RD 1007/2023 + Orden HAC/1177/2024 and states VERI*FACTU mode explicitly. Files: `src/app/mainwindow.ui` (new menu/action), `src/app/mainwindow.h/.cpp` (slot + dialog). Verifactu compliance audit table updated to COVERED in `docs/modules/verifactu/verifactu-requirements.md`.
- [x] **Temp debug code removed from `MainWindow` constructor** — early development debug code that printed module state at startup; cleaned up before the v8.0 cut.
- [x] **Verifactu fully async — UI no longer blocks on AEAT**: `VerifactuManager` and `VerifactuIntegration` rewritten — `submitInvoice` / `cancelInvoice` / `generateQRCode` (sync, `QEventLoop`-blocking) replaced with `submitInvoiceAsync` / `cancelInvoiceAsync` / `generateQRAsync` that return a `QString requestId` immediately. Results arrive via a new `requestFinished(reqId, VerifactuResult)` signal. Each consumer holds a small `QHash<reqId, ticketNum>` map to correlate.
  - **MainWindow save flow**: `saveTicket()` writes rows with `verifactu_estado = PENDIENTE` immediately. Print branching now diverges by payment state — **paid → `printFra()` only** (factura simplificada), **unpaid → `printRecibo()` only** (claim ticket, two copies). In both cases `verifactuIntegration = nullptr`, so save-time prints never carry CSV/QR (AEAT response is still in flight). The async submit is fired for paid tickets, status bar shows "Enviando ticket NNNN a AEAT..." and the form resets. When AEAT replies, `onVerifactuRequestFinished()` UPDATEs the row(s) and shows "Ticket NNNN enviado (CSV: ...)" or "Error..." in the status bar (no popups). Customer can reprint a Verifactu-complete copy with QR/CSV via `RecogPrendas → Imprimir`. `AppSettings::enablePrinting()` is checked inside `printRecibo()` / `printFra()` and only guards the actual `printTicket()` calls — the Excel file is generated even in testing mode for inspection.
  - **RecogPrendas pay-all + retry**: same async pattern. `updateDb(PAY_YES)` now consults an in-memory `hasPendingSubmit(ticketNum)` map in addition to the DB-state check (pay-all iterates per row, async submits don't update DB synchronously, so the old DB-only dedup would double-fire). `retryVerifactuSubmit` is now fire-and-forget; status bar shows progress.
  - **CancelInvoiceDialog**: async cancel; dialog stays open with "Enviando anulación..." label until AEAT replies. `m_pendingCancelId` guards against re-entry.
  - **Imprimir reprint**: `resolveQrCode()` keeps a synchronous API (print needs the QR before embedding in Excel) but built on the async core — uses a local `QEventLoop` + `QTimer::singleShot(5000, ...)` to bound the wait. On timeout, prints without the QR and logs a warning. The pre-existing DB-CSV short-circuit means save-time prints never hit this path (CSV is still empty).
  - **testConnection() stays sync** — interactive button with explicit feedback, already bounded by `setTransferTimeout(10000)`.
  - Dead-code cleanup landed alongside: removed `submitInvoices` (batch, never called), `encodeImageToBase64`, `onNetworkReply` slot, `m_lastResult` member, `createAndSubmitInvoice` (full invoice, never called), `validateEmitterConfiguration` (only caller was itself dead), `getManager()` and `getConfigInfo()` (no callers) — per `docs/dead_code_report.md`.
- [x] **Dead-code audit snapshot + agent**: `docs/dead_code_report.md` lists ~20 unused methods (mostly in `src/verifactu/` — the module was built with a broader API than `MainWindow` + `RecogPrendas` actually use). Snapshot is reproducible via the new `.claude/agents/dead-code-finder.md` subagent, which knows about Qt auto-connect slots, virtual overrides, and signals so it doesn't false-flag them. Invoke with the `Agent` tool, `subagent_type: "dead-code-finder"`. Report is informational only — no methods removed in this pass.
- [x] **"Probar conexión" button in SettingsDialog**: new button on the Verifactu tab tests the configured endpoint with the *current dialog values* (no save required). `VerifactuManager::testConnection()` was previously broken — fired a GET and returned `true` immediately, ignoring the reply. Rewritten to POST a stub `{"ServiceKey": "..."}` payload to `/Create` (GET on the base URL hangs and never returns — only POST to `/Create` actually responds), use `QEventLoop` with a 10s `setTransferTimeout`, and return a `VerifactuResult`. SUCCESS = any HTTP response (server rejects the stub with 400/401, but that still proves connectivity + auth path); NETWORK_ERROR = no HTTP status (DNS failure, refused, timeout); INVALID_CONFIG = local validation failed. The dialog emits `testConnectionRequested(nif, name, key, production)`; `MainWindow` builds a transient `VerifactuManager`, runs the test, and shows the result via `QMessageBox` — keeps `appsettings` from depending on `verifactu`.
- [x] **Em-dash purged from source code**: 49 occurrences across 13 `.cpp` / `.h` files (comments, log strings, a few user-visible Spanish strings like the Verifactu dialog window title and empty-value placeholders) replaced with ASCII `-`. Some Windows code pages mangle U+2014 in source files. Coding guidelines updated with a "no non-ASCII punctuation in source" rule (em-dash, en-dash, curly quotes, ellipsis) so future agents don't reintroduce them.
- [x] **Log de depuración dialog opens the log file directly**: `on_actionMostrar_log_triggered()` button relabelled from "Abrir carpeta" to "Abrir archivo"; `QDesktopServices::openUrl` now points to `path` instead of `QFileInfo(path).absolutePath()`. Faster for support — customer can attach the file with one click instead of navigating Explorer.
- [x] **Per-garment payment disabled in `RecogPrendas`**: `pb_payment` button is no longer re-enabled on row selection — payment can only be marked via "Pagar todo" (`pb_pay_all`), which submits one Verifactu invoice per ticket. Prevents the duplicate-InvoiceID rejection from AEAT that would occur if the user paid one garment, triggered an F2 with `InvoiceID = n_recibo`, then later paid another garment for the same ticket. The `on_pb_pay_all_clicked()` loop calls `on_pb_payment_toggled(true)` programmatically, which still fires on a disabled button — full-ticket pay flow unchanged. See "Per-payment Verifactu InvoiceID seq" in Open Non-Blocking Issues for the architectural follow-up that would restore per-garment payment.
- [x] **Verifactu conditional on payment**: `on_bb_save_reset_clicked()` now only calls `verifactuSubmitInvoice()` when `pagado == "SI"`; unpaid saves produce NotSubmitted state. `updateDb(PAY_YES)` in `recog_prendas.cpp` auto-submits if the ticket is still NotSubmitted, using the payment date as invoice date; DB is queried directly so the pay-all loop never double-submits.

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
- [x] `NotSubmitted` now serializes as `"PENDIENTE"` instead of empty string — user-visible status in the Verifactu info dialog and Anular factura search. `verifactuEstadoFromString()` still maps NULL/empty to `NotSubmitted` for legacy pre-Verifactu rows

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
