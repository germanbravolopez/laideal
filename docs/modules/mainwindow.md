# MainWindow (`src/app/`)

Central application controller. Owns the SQLite `db` connection and instantiates all child windows on demand.

## Window

Built in code with the shared [UiKit](uikit.md) style (`mainwindow.ui` was removed in October 2026; the menus and their 25 actions are created in `buildUi()` with the former object names, texts, tooltips and shortcuts, so the `on_action<Name>_triggered` slots still connect by name). Top to bottom: the shop name as heading (`AppSettings::businessName`) with quick buttons on the right - **Recogida de prendas**, **Añadir prendas**, **Factura de gastos** (they trigger the menu actions); an explanation panel; a **Cliente** group (editable client combo, Teléfono, Móvil, Dirección) beside a **Ticket** group (read-only, bold Nº recibo; Recepción date; **Pagado al dejarlo** checkbox with a green SÍ / red NO label); a **Prendas** group (the ticket table - Prenda takes the spare width, amounts right-aligned -, **Añadir fila**, the read-only bold Importe total); **Limpiar** / the primary **Guardar ticket**; a result panel; the status bar (AEAT replies, automatic backups).

Every message is written in the result panel: the save summary (ticket, client, garments, amount, paid or not, what was printed, the client-name note), each validation refusal, Verifactu not available at start-up, a failed backup, and the results of the menu tools (log file link, backup with link, update check). Only the Settings dialog's connection test keeps its own message boxes.

**Start-up messages stack**: `mainwindowInitialSettings()` puts its start problems in the panel (failed pre-migration backup, incomplete migration, backup warning; "Listo para un nuevo ticket." only when there are none); `initializeVerifactu()` (Verifactu not available, direct-connection warnings, broken stored chain) and the post-update notice then add below with `appendToResult()`, so none hides another. Later results (a save, a menu tool) replace the panel as usual.

The menu bar has one menu per kind of work, all built in `MainWindow::buildUi()`. The UI smoke test opens them by their visible text, so a renamed entry must be renamed in `tests/ui` too:

| Menu | Entries |
|------|---------|
| **Archivo** | Configuración… · Hacer copia de seguridad ahora · Cerrar (Alt+C) |
| **Tickets** | Recogida de prendas… (Alt+R) · Añadir nuevas prendas… (Alt+Shift+N) · Imprimir ▸ Recibo… (Alt+P, R) / Factura… (Alt+P, F) / Factura completa… (Alt+P, Alt+F) |
| **Facturas** | Anular factura Verifactu… (Alt+A) · Rectificar factura Verifactu… (Alt+T) · Exportar registros AEAT (XML)… |
| **Contabilidad** | Añadir factura de gastos… (Alt+F) · Generar contabilidad… (Alt+G) · Revertir contabilidad… (no shortcut, so a closed quarter is never reopened by one key press) |
| **Ver** | Ingresos (Alt+Shift+I) · Gastos (Alt+Shift+G) · Listado de prendas / clientes / proveedores / servicios (Alt+Shift+P / C / O / S) |
| **Ayuda** | Buscar actualizaciones · Notas de la versión · Comprobar tras actualizar… · Log de depuración · Acerca de Verifactu |

"…" marks an entry that opens a window to fill in or choose from before anything happens; an entry that acts at once (the backup, the update check) or only shows something (Ver, the release notes, the log, Acerca de Verifactu) has none. The top bar repeats Recogida de prendas, Añadir prendas and Factura de gastos as quick buttons. `Configuración…` is created with the other actions and connected in the constructor once the database is migrated.

**Ticket entry**: a garment picked before its quantity counts 1 and is priced at once; prices follow the row that was edited (the combo's own row, not the table's current row, which a combo inside a cell does not move); an m2 garment shows 0,00 until its size is entered (`sql_lite::garmentUnmeasured`, the rule Recogida and Cobrar use too) and the ticket can be left **unpaid** like that - a ticket marked *Pagado al dejarlo* with an unmeasured m2 garment is refused, since its invoice would go to AEAT at 0 and could never be charged; a hand-typed price is kept (decimal comma accepted, shown in cents; a negative one is refused - corrections go through Rectificar factura); the total is always the sum of the rows and cannot be typed; slots with no garment are never saved, and the amount sent to AEAT is the sum of the rows actually stored (`saveTicket()` returns it). Saving into a closed quarter is refused (`quarterIsClosed`, the whole quarter).

## Source files

The `laideal_app` library (`src/app/CMakeLists.txt`) holds everything but `main.cpp`, so the e2e bench can link it:

| File | Role |
|------|------|
| `main.cpp` | Entry point: icon, language, the command-line switches below, the missing-database prompt, the single-instance lock, then `MainWindow`. The exe is only this file plus the resources |
| `commandline.h` | `CommandLine::switchValue`: the value after a switch, and whether the switch is dangling (last argument) |
| `mainwindow.h/cpp` | Ticket entry window and the menus (this page) |
| `cancelinvoicedialog.*`, `rectifyinvoicedialog.*` | Facturas → Anular / Rectificar (below) |
| `pendingsubmitsdialog.*` | Start-up recovery of PENDIENTE payments: Reintentar / Error / Posponer per (ticket, seq) |
| `refusedinvoicesdialog.*` | The list behind the red "N facturas rechazadas" status-bar count; double click opens the ticket in Recogida |
| `aeatexport.*`, `aeatexportdialog.*` | Facturas → Exportar registros AEAT (XML): one record per payment event |
| `selfcheck.*`, `selfcheckdialog.*` | The post-update check (`--self-check`) on a copy of the newest backup |
| `instancelock.*` | One copy of the app per database (`QLockFile`) |

Command-line switches (`main.cpp`): `--settings <file>` runs with another settings file (a test run on the shop PC; a `--settings` with no file after it is an error, exit code 1, never the shop's own settings); `--self-check` opens only the post-update check (exit code 0 when every check is correct or omitted, 1 otherwise or when the window is closed before the questions are answered; the report is still written); `--fake-aeat <http://127.0.0.1:port>` exists in the UI-test build only (CMake `LAIDEAL_UI_TEST`) and sends the direct AEAT client to a local fake. When the database path is unset or missing, the Settings dialog opens first.

## Key methods

| Method | Purpose |
|--------|---------|
| `mainwindowInitialSettings()` | Start-up: pre-migration backup, `migrateDatabase()`, post-update notice, backup warning, the Configuración action, table columns |
| `initializeVerifactu()` | One `VerifactuIntegration` for the window's life, re-initialised after Configuración (the windows holding it keep working): warns if not configured (paid saves are then refused, unpaid ones still saved), connects it once (`connectVerifactu`), settles rectificativas (`settlePendingRectifications`) and, for the direct client, shows its warnings, verifies the stored chain and starts the chain sync (`continueDirectChain`) |
| `appendToResult(html)` | Adds a message below what the result panel already shows (start-up messages) |
| `connectVerifactu()` / `onRecordSettled()` | Once-only connections: replies nobody waited for (records sent at start or retried by the direct client) still settle their rows; a finished chain sync refreshes the next ticket number |
| `updateAeatEnvironment()` | The status-bar notice that the direct client is in AEAT's test environment |
| `updateRefusedInvoices()` / `showRefusedInvoices()` | The red status-bar count of invoices AEAT refused (refreshed on every outcome and every 30 s) and the list behind it |
| `showVerifactuRejection()` | A definitive AEAT refusal in the result panel (it can come after the factura was printed) |
| `verifactuQrBeforeReply()` | With the direct client the QR is known before AEAT answers: the factura prints with it at once |
| `runAeatSelfTest()` | The "Prueba con la AEAT" window of the Settings dialog |
| `resetAllContents()` | Clears form after a ticket save |
| `setNextTicketNumber()` | Auto-increments `n_recibo` from the max in `ingresos` |
| `populateCbClient()` | Fills the client combobox from `clientes` table |
| `validateTicket()` | Pre-save checks: client, garments and prices (m2 unmeasured, negative, unpriced), dates, closed quarter, and the chain gate (`numberingBlockedReason`: no paid ticket while the direct client's first check runs) |
| `checkClientData()` | Adds or updates the client row in `clientes` |
| `verifactuSubmitInvoice(ticketNum, date, total)` | Fires `VerifactuIntegration::submitSimplifiedInvoiceAsync()`, tracks `reqId → ticketNum` in `m_pendingSubmits`, shows status-bar progress |
| `onVerifactuRequestFinished(reqId, result)` | Slot — looks up the ticket, UPDATEs `verifactu_*` columns, updates status bar |
| `saveTicket()` | Inserts the N garment rows into `ingresos` in one transaction (`insertGarmentRows`, all or none) with `verifactu_estado = PENDIENTE` (paid) / `SIN COBRAR`; returns the stored total; async submit patches the rows when AEAT replies |
| `printRecibo()` / `printFra()` | Build the two copies through `Imprimir` (`verifactuIntegration = nullptr`: no QR fetch at save time) and print them when `AppSettings::enablePrinting()` is on; return whether both reached the printer (reported in the save summary). |
| `on_actionAnular_factura_verifactu_triggered()` | Opens `CancelInvoiceDialog` (paid/ENVIADA rows → AEAT anulación); shows warning if Verifactu not configured |
| `on_actionRectificar_factura_verifactu_triggered()` | Opens `RectifyInvoiceDialog` (R1-R5 factura rectificativa); shows warning if Verifactu not configured. Art. 8.2.a RD 1007/2023 |
| `on_actionAcerca_de_Verifactu_triggered()` | Opens the Ayuda → Acerca de Verifactu dialog showing the fixed-text declaración responsable required by Art. 13 RD 1007/2023. Producer NIF/name/address come from `AppSettings`; software version comes from `PROJECT_VERSION_MAJOR/MINOR` in the generated `version.h` |

## Table column indices (mainwindow.h)

| Constant | Index | Column |
|----------|-------|--------|
| `TABLE_TICKET_QNTY` | 0 | Quantity |
| `TABLE_TICKET_GARM` | 1 | Garment |
| `TABLE_TICKET_SIZE` | 2 | Size (m²) |
| `TABLE_TICKET_SERV` | 3 | Service |
| `TABLE_TICKET_OBSE` | 4 | Observations |
| `TABLE_TICKET_PRIC` | 5 | Price |

## Ticket save flow

```
on_pb_save_clicked()             — Guardar ticket (Qt auto-connect slot)
  ├── validateTicket()
  ├── checkClientData()
  ├── saveTicket()                  — N rows with verifactu_estado = PENDIENTE
  ├── if (isPaid):
  │     ├── verifactuSubmitInvoice  — fires async; status bar "Enviando..."
  │     └── printFra()              — factura simplificada: with the QR at once for the direct client
  │                                   (verifactuQrBeforeReply), without it for the gateway
  ├── else:
  │     └── printRecibo()           — claim receipt, two copies
  ├── resetAllContents()
  └── result panel: "Ticket N guardado: ..."
```

When the AEAT reply arrives, `onVerifactuRequestFinished()` UPDATEs `verifactu_csv` / `verifactu_timestamp` / `verifactu_estado` for the ticket and posts the result to the status bar. With the gateway the save-time print has no CSV/QR; the customer can reprint a Verifactu-complete copy via `RecogPrendas → Imprimir` once status arrives.

## Child windows

Opened via menu actions. Each child holds its own `db` reference set by `MainWindow` at open time:
`Listado`, `RecogPrendas`, `Facturas`, `Contabilidad`, `Imprimir`, `AddGarment`, `CancelInvoiceDialog`, `VoidGarmentsDialog`, `RectifyInvoiceDialog`, `PendingSubmitsDialog` (start-up), `RefusedInvoicesDialog`, `AeatExportDialog`, `SelfCheckDialog` (separate process, `--self-check`)

`CancelInvoiceDialog` (`src/app/cancelinvoicedialog.h/cpp`) is a modal dialog (no `.ui` file) opened from Facturas → Anular factura Verifactu. It searches `ingresos` by ticket number, shows Verifactu details, asks for confirmation, calls `VerifactuIntegration::cancelInvoiceAsync()` and, on success, updates `verifactu_estado` to `ANULADA` for the rows of one payment event (ticket, `verifactu_invoice_seq`): there is one row and one Anular button per event, sent with the payment date as the AEAT date (a retry with the reception date for older data), refused while the current quarter is closed, while the chain is unchecked (`numberingBlockedReason`) and while a rectificativa of the ticket is live or pending; with the direct client the cancellation is queued and settles later (`onRecordSettled`). While a cancellation waits for AEAT, the ticket field and Buscar are locked, and the reply applies to the ticket and payment event it was sent for (`m_pendingTicket` / `m_pendingEvent`), never to the rows the window shows by then.

`VoidGarmentsDialog` (`src/recog_prendas/voidgarmentsdialog.h/cpp`, UiKit style) is a modal dialog opened from **Recogida de prendas → Anular prendas…** with the selected garment's ticket already loaded (`loadTicket`; the Herramientas menu entry was removed in October 2026, and the dialog's own Nº recibo field still lets another ticket be searched) — the local counterpart to `CancelInvoiceDialog` for issue #40 (erroneous receipts / change of mind on garments not yet delivered). It searches `ingresos` by ticket number and lists the ticket's garments with per-garment checkboxes; only rows passing `sql_lite::garmentIsLocallyVoidable(pagado, verifactuEstado)` (unpaid AND never sent to AEAT) are selectable. Voiding a selected row calls `sql_lite::voidGarmentRow(db, nRecibo, hash)` → `estado='Anulado'`, `verifactu_estado='ANULADA'`, `fecha_anulacion` = today, with `fecha_pago` and `fecha_recogida` left empty because the garment was never paid nor collected (leaving `pagado='NO'`). Because these rows were never submitted to AEAT, there is **no** AEAT call — paid/ENVIADA rows must use `CancelInvoiceDialog`. `TextColorDelegate` renders any `Anulado` / `verifactu_estado = ANULADA` row green so its `NO` pagado no longer reads as a debt. Once voided, an `Anulado` row is **immutable and excluded everywhere**: RecogPrendas disables its pay/pickup/split buttons and read-onlys its fields — **except `observaciones`, which stays writable** so staff can note why it was voided (with an `updateDb` early-return as defense-in-depth for every op but `OBSV`), `PayDialog::loadTicket` skips it (never chargeable → never submitted to AEAT), `sql_lite::markTicketPickedUp` (Recoger todo) excludes it, and `Imprimir::getTicketInfo` excludes it from recibo/factura prints. Contabilidad needs no change — an `Anulado` row is `pagado='NO'` (accounting counts only `pagado='SI'`) and `verifactu_estado='ANULADA'` (excluded from the Verifactu figures).

`RectifyInvoiceDialog` (`src/app/rectifyinvoicedialog.h/cpp`) is a modal dialog (no `.ui` file) opened from Facturas → Rectificar factura Verifactu. It searches `ingresos` by ticket number, lets the operator pick R1-R5 + sustitución/diferencias (S/I) + corrected total or delta + date, calls `VerifactuIntegration::submitRectificationAsync()`, and, as the AEAT reply arrives, completes a NEW `ingresos` row with its own number (`R-...` series), `verifactu_rectifies_n_recibo` pointing back to the original and the rectification's own CSV/XML/hash. The row is inserted first as a PENDIENTE placeholder (before the request, so a failed INSERT sends nothing) and patched on the reply; a refused rectificativa is reused under its own number when resent; the direct client allows R5 only, a ticket with several partial payments is blocked, and the rectification date is bounded (not in the future, not in a closed quarter). The reply sets ENVIADA. For substitution mode (`S`) it additionally marks the original rows `verifactu_estado = RECTIFICADA` so they are excluded from the Contabilidad income predicate (`kIngresosIncomeWhere`). For differences (`I`) the original rows stay `ENVIADA` and the delta row alone reconciles accounting. The rectificativa row is inserted before sending; if that INSERT fails nothing is sent to AEAT. While the request waits, the ticket field and Buscar are locked; the reply, and the destructor's "closed before the answer" path, act on the ticket it was sent for (`m_submittedTicket`), and a reply UPDATE that finds no row is reported as a local failure, never as "Rectificativa enviada".

## Known issues

- Verifactu `m_verifactuIntegration` member is heap-allocated with `this` as Qt parent (no manual delete needed).
- The findings of the pre-12.0 review of this module (`docs/reviews/2026-10-19-pre-12.0/app.md`) are fixed, except the few left to the owner there (export overwrite prompt, content of the export for unaccepted records); open items live in `docs/progress_tracker.md`.
