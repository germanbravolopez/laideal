# MainWindow (`src/app/`)

Ticket-entry window and central controller: owns the SQLite `db` connection, builds the menus and opens every other window on demand. Verifactu rules: [verifactu/README.md](verifactu/README.md).

## Window

Built in code (`buildUi`, shared [UiKit](uikit.md) style); there is no `mainwindow.ui`. The menus and their actions are created there with their object names, so the `on_action<Name>_triggered` and `on_<widget>_<signal>` slots connect by name and the UI smoke test finds entries by visible text (renaming an entry means renaming it in `tests/ui`).

Top to bottom: shop name (`AppSettings::businessName`) with quick buttons (Recogida de prendas, Añadir prendas, Factura de gastos); an explanation panel; **Cliente** group (client combo, phones, address) beside the **Ticket** group (read-only Nº recibo, reception date, **Pagado al dejarlo** with a SÍ / NO label); the **Prendas** table (`kInitialTicketRows` rows, **Añadir fila**, read-only Importe total); **Limpiar** / **Guardar ticket**; a result panel; the status bar.

**Result panel**: every message goes there (save summary, validation refusals, failed backup, tool results). Only the Settings dialog's connection test uses message boxes. Start-up messages stack: `mainwindowInitialSettings` writes first, then `initializeVerifactu` and the post-update notice add with `appendToResult`; later results replace the panel.

**Status bar** (`buildStatusBar`, three notices in order): the AEAT environment (`updateAeatEnvironment`: direct client only; "entorno de PRUEBAS" warning, or "PRODUCCIÓN desde el <day>"), the red count of invoices AEAT refused (`updateRefusedInvoices`, refreshed on each outcome and every 30 s, hidden at 0), and the count of records still to send after an incident (`updateUnsentRecords`). AEAT replies and backups also report here.

| Menu | Entries |
|------|---------|
| **Archivo** | Configuración... · Hacer copia de seguridad ahora · Cerrar |
| **Tickets** | Recogida de prendas... · Añadir nuevas prendas... · Imprimir > Recibo / Factura / Factura completa |
| **Facturas** | Anular factura Verifactu... · Rectificar factura Verifactu... · Exportar registros AEAT (XML)... |
| **Contabilidad** | Añadir factura de gastos... · Generar contabilidad... · Revertir contabilidad... (no shortcut, so a quarter is never reopened by one key) |
| **Ver** | Ingresos · Gastos · Listado de prendas / clientes / proveedores / servicios |
| **Ayuda** | Buscar actualizaciones · Notas de la versión · Comprobar tras actualizar... · Log de depuración · Acerca de Verifactu |

"..." marks an entry that opens a window to fill in before anything happens.

## Ticket entry rules

- A garment picked before its quantity counts 1 and is priced at once; prices follow the row edited (`updateRowPrice`).
- An "m2" garment shows 0,00 until its size is entered (`garmentUnmeasured`). The ticket may be left unpaid like that; **Pagado al dejarlo** with one is refused.
- A hand-typed price is kept (decimal comma accepted; negative refused: corrections go through Rectificar factura). The total is the sum of the rows and cannot be typed. Empty slots are not saved.
- Saving into a closed quarter is refused (`quarterIsClosed`, whole quarter).
- `validateTicket` also refuses: no client / garments, unpriced rows, a paid ticket dated before production (`dateBeforeProductionReason`), and, while the direct client's first chain check runs, any paid ticket (`numberingBlockedReason`, `m_numberingRefused`).

## Save flow

```
on_pb_save_clicked          (m_saving blocks a second click while waiting for AEAT)
  validateTicket
  saveTicket                all garment rows in one transaction (insertGarmentRows), PENDIENTE paid / SIN COBRAR unpaid; returns the stored total
  checkClientData           adds or updates the client, only for a stored ticket
  submitAndPrint
    unpaid:  printRecibo
    paid:    verifactuSubmitInvoice (async, amount = stored total)
             direct client: QR known at once (verifactuQrBeforeReply), print the factura
             gateway:       waitForAeatReply (up to 3 s, window disabled), then print
  resetAllContents
  savedMessage              ticket, client, garments, amount, paid or not, what printed, client note
```

`onVerifactuRequestFinished` patches the rows' `verifactu_*` columns when the reply arrives (`updateTicketVerifactuFields`); a late success after a QR-less print says the factura is now printable (`m_pendingSubmits`, `printedWithoutQr`). `printRecibo` / `printFra` use `Imprimir` and print only when `AppSettings::enablePrinting()` is on.

## Methods

| Method | Purpose |
|--------|---------|
| `mainwindowInitialSettings` | Start-up: pre-migration backup, `migrateDatabase`, post-update notice, backup warning, table setup. |
| `initializeVerifactu` | One `VerifactuIntegration` for the window's life (re-initialised after Configuración): warns if not configured (paid saves refused), `connectVerifactu`, `settlePendingRectifications`, and for the direct client its warnings, chain verification and sync (`continueDirectChain`). |
| `connectVerifactu` / `onRecordSettled` | Once-only connections: outcomes nobody waited for still settle their rows; a finished chain sync refreshes the next number. |
| `showPendingSubmits` | The start-up recovery dialog of PENDIENTE payments (`PendingSubmitsDialog`); retries through `verifactuSubmitInvoice(ticket, date, total, seq)`. Waits (retries every 2 s) while a modal dialog is open. |
| `recoveryFloorIso` | The floor for the pending / refused lists (see `verifactuRecoveryFloorIso`). |
| `showRefusedInvoices`, `showVerifactuRejection`, `rejectionHtml` | The refused list behind the red count; a definitive refusal in the result panel. |
| `openSettings` | Configuración dialog, and its connection tests (`checkAeatProduction`, `testGatewayConnection`, `runAeatSelfTest`, `runAeatConnectionTest`). |
| `openListado(table, objectName, title)`, `openImprimir(isRecibo, isCompleteInvoice, title)` | One opener for the four Ver > Listado entries and the three Imprimir entries. |
| `setNextTicketNumber`, `populateCbClient`, `repopulatePrendas`, `repopulateClientes` | Next `n_recibo` (via `nextTicketNumber`; unreadable: no number and saving refused) and the combos. |

## Windows opened

Each takes the `db` reference at open time: `Listado`, `RecogPrendas`, `Facturas`, `Contabilidad`, `Imprimir`, `AddGarment`, `CancelInvoiceDialog`, `RectifyInvoiceDialog`, `RefusedInvoicesDialog`, `AeatExportDialog`, `PendingSubmitsDialog` (start-up), `SelfCheckDialog`. `VoidGarmentsDialog` is opened from [RecogPrendas](recog_prendas.md).

`AddGarment` gets its `InvoicingPolicy` from `invoicingPolicyOf(integration)` (why invoicing is blocked, the payment date's production limit, the AEAT environment); on `paidGarmentSaved` it calls `verifactuSubmitInvoice`. See [add_garment.md](add_garment.md).

| Dialog | Does |
|--------|------|
| `CancelInvoiceDialog` | Facturas > Anular. Searches a ticket, one row and Anular button per payment event; sends the cancellation (`cancelInvoiceAsync`), then `markInvoiceSeqCancelled`. Refused while the current quarter is closed, the chain is unchecked, or a rectificativa of the ticket is live or pending. The reply applies to the ticket and event it was sent for (`m_pendingTicket` / `m_pendingEvent`); the ticket field and Buscar are locked meanwhile. With the direct client it settles later (`onRecordSettled`). |
| `RectifyInvoiceDialog` | Facturas > Rectificar. R1-R5, S / I, total or delta, date (not future, not in a closed quarter). Inserts the `R-<n>` row PENDIENTE first (a failed insert sends nothing), submits (`submitRectificationAsync`), patches on the reply; a substitution marks the original RECTIFICADA. Direct client allows R5 only; a ticket with several partial payments is blocked; a refused rectificativa is resent under its own number. Replies act on `m_submittedTicket`. |
| `PendingSubmitsDialog` | Reintentar / Error / Posponer per (ticket, seq). |
| `RefusedInvoicesDialog` | The refused list; double click opens the ticket in Recogida (`showTicket`). |
| `AeatExportDialog`, `aeatexport.*` | Exportar registros AEAT (XML): one record per payment event. |
| `SelfCheckDialog`, `selfcheck.*` | The post-update check on a copy of the newest backup (`--self-check`, separate process). |

## Other files

| File | Role |
|------|------|
| `main.cpp` | Entry point: icon, language, command-line switches, missing-database prompt (Settings dialog first), single-instance lock, then `MainWindow`. |
| `commandline.h` | `CommandLine::switchValue`: the value after a switch, and whether it is dangling. |
| `instancelock.*` | One copy of the app per database (`QLockFile`). |
| `version.h.in` | Generated `version.h` (`PROJECT_VERSION_*`), the single version source. |

Switches: `--settings <file>` (another settings file; no file after it is an error, exit 1); `--self-check` (only the post-update check; exit 0 when every check is correct or omitted); `--fake-aeat <url>` (UI-test build `LAIDEAL_UI_TEST` only: the direct client talks to a local fake).

`laideal_app` (`src/app/CMakeLists.txt`) holds everything except `main.cpp`, so the e2e bench can link it.

## Table columns (`mainwindow.h`)

`TABLE_TICKET_QNTY` 0, `_GARM` 1, `_SIZE` 2, `_SERV` 3, `_OBSE` 4, `_PRIC` 5.

## Tests

`test_e2e_app` and `test_e2e_verifactu` drive the window (e.g. `test_mainWindow_failedInsertStopsTheSave`), `tests/ui` opens the menus, `test_selfcheck` and `test_instancelock` cover their files. See [testing](../testing/README.md).
