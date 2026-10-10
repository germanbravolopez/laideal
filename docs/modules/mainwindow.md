# MainWindow (`src/app/`)

Central application controller. Owns the SQLite `db` connection and instantiates all child windows on demand.

## Window

Built in code with the shared [UiKit](uikit.md) style (`mainwindow.ui` was removed in October 2026; the menus and their 25 actions are created in `buildUi()` with the former object names, texts, tooltips and shortcuts, so the `on_action<Name>_triggered` slots still connect by name). Top to bottom: the shop name as heading (`AppSettings::businessName`) with quick buttons on the right - **Recogida de prendas**, **Añadir prendas**, **Factura de gastos** (they trigger the menu actions); an explanation panel; a **Cliente** group (editable client combo, Teléfono, Móvil, Dirección) beside a **Ticket** group (read-only, bold Nº recibo; Recepción date; **Pagado al dejarlo** checkbox with a green SÍ / red NO label); a **Prendas** group (the ticket table - Prenda takes the spare width, amounts right-aligned -, **Añadir fila**, the read-only bold Importe total); **Limpiar** / the primary **Guardar ticket**; a result panel; the status bar (AEAT replies, automatic backups).

Every message is written in the result panel: the save summary (ticket, client, garments, amount, paid or not, what was printed, the client-name note), each validation refusal, Verifactu not available at start-up, a failed backup, and the results of the Archivo / Herramientas / Ayuda tools (database clean-up, hashes, log file link, backup with link, update check). Only the Settings dialog's connection test keeps its own message boxes.

**Herramientas** is grouped by function, separators between the groups: Recogida (Recogida de prendas, Añadir nuevas prendas, Imprimir ▸ Recibo / Factura / Factura completa) · Verifactu (Anular factura, Rectificar factura, Exportar registros AEAT) · Gastos (Formulario facturas) · Contabilidad (Generar, Revertir) · Hacer copia de seguridad.

**Ticket entry**: a garment picked before its quantity counts 1 and is priced at once; prices follow the row that was edited (the combo's own row, not the table's current row, which a combo inside a cell does not move); an m2 garment shows 0,00 until its size is entered (`sql_lite::garmentUnmeasured`, the rule Recogida and Cobrar use too) and the ticket can be left **unpaid** like that - a ticket marked *Pagado al dejarlo* with an unmeasured m2 garment is refused, since its invoice would go to AEAT at 0 and could never be charged; a hand-typed price is kept (decimal comma accepted, shown in cents; a negative one is refused - corrections go through Rectificar factura); the total is always the sum of the rows and cannot be typed; slots with no garment are never saved, and the amount sent to AEAT is the sum of the rows actually stored (`saveTicket()` returns it). Saving into a closed quarter is refused (`quarterIsClosed`, the whole quarter).

## Source files

- `src/app/main.cpp` — entry point; loads the bundled app icon from the Qt resource `:/icons/laideal.ico` (also embedded in the exe as `IDI_ICON1` for Explorer/shortcut visibility)
- `src/app/mainwindow.h/cpp` — main window class

## Key methods

| Method | Purpose |
|--------|---------|
| `mainwindowInitialSettings()` | UI setup at startup: runs `migrateDatabase()`, configures table columns, buttons, comboboxes |
| `initializeVerifactu()` | Creates `VerifactuIntegration`; shows non-fatal warning if not configured |
| `resetAllContents()` | Clears form after a ticket save |
| `setNextTicketNumber()` | Auto-increments `n_recibo` from the max in `ingresos` |
| `populateCbClient()` | Fills the client combobox from `clientes` table |
| `validateTicket()` | Pre-save checks: client present, garments valid, quarter not locked |
| `checkClientData()` | Adds or updates the client row in `clientes` |
| `verifactuSubmitInvoice(ticketNum, date, total)` | Fires `VerifactuIntegration::submitSimplifiedInvoiceAsync()`, tracks `reqId → ticketNum` in `m_pendingSubmits`, shows status-bar progress |
| `onVerifactuRequestFinished(reqId, result)` | Slot — looks up the ticket, UPDATEs `verifactu_*` columns, updates status bar |
| `saveTicket()` | Inserts the N garment rows into `ingresos` in one transaction (`insertGarmentRows`, all or none) with `verifactu_estado = PENDIENTE` (paid) / `SIN COBRAR`; returns the stored total; async submit patches the rows when AEAT replies |
| `printRecibo()` / `printFra()` | Build the two copies through `Imprimir` (`verifactuIntegration = nullptr`: no QR fetch at save time) and print them when `AppSettings::enablePrinting()` is on; return whether both reached the printer (reported in the save summary). |
| `cleanDatabase(print)` | Fixes comma decimal separators in DB |
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
  │     └── printFra()              — factura simplificada (no CSV/QR yet)
  ├── else:
  │     └── printRecibo()           — claim receipt, two copies
  ├── resetAllContents()
  └── result panel: "Ticket N guardado: ..."
```

When the AEAT reply arrives, `onVerifactuRequestFinished()` UPDATEs `verifactu_csv` / `verifactu_timestamp` / `verifactu_estado` for the ticket and posts the result to the status bar. The save-time print never has the CSV/QR; the customer can reprint a Verifactu-complete copy via `RecogPrendas → Imprimir` once status arrives.

## Child windows

Opened via menu actions. Each child holds its own `db` reference set by `MainWindow` at open time:
`Listado`, `RecogPrendas`, `Facturas`, `Contabilidad`, `Imprimir`, `AddGarment`, `CancelInvoiceDialog`, `VoidGarmentsDialog`, `RectifyInvoiceDialog`

`CancelInvoiceDialog` (`src/app/cancelinvoicedialog.h/cpp`) is a modal dialog (no `.ui` file) opened from Herramientas → Anular factura Verifactu. It searches `ingresos` by ticket number, shows Verifactu details, calls `VerifactuIntegration::cancelInvoiceAsync()`, and on success updates `verifactu_estado` to `ANULADA` for all rows of that ticket.

`VoidGarmentsDialog` (`src/recog_prendas/voidgarmentsdialog.h/cpp`, UiKit style) is a modal dialog opened from **Recogida de prendas → Anular prendas…** with the selected garment's ticket already loaded (`loadTicket`; the Herramientas menu entry was removed in October 2026, and the dialog's own Nº recibo field still lets another ticket be searched) — the local counterpart to `CancelInvoiceDialog` for issue #40 (erroneous receipts / change of mind on garments not yet delivered). It searches `ingresos` by ticket number and lists the ticket's garments with per-garment checkboxes; only rows passing `sql_lite::garmentIsLocallyVoidable(pagado, verifactuEstado)` (unpaid AND never sent to AEAT) are selectable. Voiding a selected row calls `sql_lite::voidGarmentRow(db, nRecibo, hash)` → `estado='Anulado'`, `verifactu_estado='ANULADA'`, `fecha_anulacion` = today, with `fecha_pago` and `fecha_recogida` left empty because the garment was never paid nor collected (leaving `pagado='NO'`). Because these rows were never submitted to AEAT, there is **no** AEAT call — paid/ENVIADA rows must use `CancelInvoiceDialog`. `TextColorDelegate` renders any `Anulado` / `verifactu_estado = ANULADA` row green so its `NO` pagado no longer reads as a debt. Once voided, an `Anulado` row is **immutable and excluded everywhere**: RecogPrendas disables its pay/pickup/split buttons and read-onlys its fields — **except `observaciones`, which stays writable** so staff can note why it was voided (with an `updateDb` early-return as defense-in-depth for every op but `OBSV`), `PayDialog::loadTicket` skips it (never chargeable → never submitted to AEAT), `sql_lite::markTicketPickedUp` (Recoger todo) excludes it, and `Imprimir::getTicketInfo` excludes it from recibo/factura prints. Contabilidad needs no change — an `Anulado` row is `pagado='NO'` (accounting counts only `pagado='SI'`) and `verifactu_estado='ANULADA'` (excluded from the Verifactu figures).

`RectifyInvoiceDialog` (`src/app/rectifyinvoicedialog.h/cpp`) is a modal dialog (no `.ui` file) opened from Herramientas → Rectificar factura Verifactu. It searches `ingresos` by ticket number, lets the operator pick R1-R5 + sustitución/diferencias (S/I) + corrected total or delta + date, calls `VerifactuIntegration::submitRectificationAsync()`, and on success inserts a NEW `ingresos` row with the next available `n_recibo`, `verifactu_estado = ENVIADA`, `verifactu_rectifies_n_recibo` pointing back to the original and the rectification's own CSV/XML/hash. For substitution mode (`S`) it additionally marks the original rows `verifactu_estado = RECTIFICADA` so they are excluded from the Contabilidad income predicate (`kIngresosIncomeWhere`). For differences (`I`) the original rows stay `ENVIADA` and the delta row alone reconciles accounting.

## Known issues

- Verifactu `m_verifactuIntegration` member is heap-allocated with `this` as Qt parent (no manual delete needed).
