# End-to-end test bench

Runs the real application objects together — not one module in isolation — against a seeded throwaway database and a **fake Verifactu server**, so whole flows (ticket save / payment → AEAT submission → reply → estado / QR → cancellation / rectification / recovery → Contabilidad) are checked on every CI run without ever reaching the real AEAT. Conventions shared with the other suites are in [README.md](README.md).

Two suites: `test_e2e_verifactu` (phase 1: PayDialog and the Verifactu client) and `test_e2e_app` (phase 2: MainWindow, the app dialogs, Imprimir and the Contabilidad form at screen level).

---

## Safety rules

The bench must never reach the real AEAT nor change the shop's database or settings:

- **Endpoint**: the test-only seam `VerifactuConfig::setEndpointOverride(baseUrl)` points every Verifactu call (Create, Cancel, GetFilteredList, GetQrCode) at the local fake server. Only code linked into a test can call it; nothing in the settings, environment or registry reaches it, so a shop install can never divert real invoices. The suite **refuses to start** unless the resolved endpoint is `http://127.0.0.1:…`.
- **Database**: a throwaway SQLite file in a `QTemporaryDir`, created and seeded by the test.
- **Settings**: a throwaway file via `AppSettings::loadFrom`, so every save goes there and `~/.laideal_settings.json` is never read or written. Printing is disabled (`enablePrinting = false`); the ticket is still built. `test_e2e_app` also turns off the startup update check and backup, points the reports root at the temp dir, and sets `Contabilidad::setOpenGeneratedReports(false)` so a generated PDF is not opened in a viewer.

---

## Building blocks — `tests/support/`

### `FakeVerifactuServer`

A minimal HTTP/1.1 server on `127.0.0.1` (random port) standing in for the IreneSolutions REST service.

- `start()` / `baseUrl()` — pass `baseUrl()` to `VerifactuConfig::setEndpointOverride`.
- **Records every request**: `requests()` / `requestsTo("Create")` give the endpoint and the parsed JSON body, so a test can assert what was sent (invoice id, amount, service key).
- **Scripted replies**: `enqueue(endpoint, Reply)` answers the next request to that endpoint with a given body, after `delayMs` (simulates a slow AEAT), or `drop`s the connection (no answer). Later requests fall back to the default.
- **Default replies**: Create / Cancel → accepted (incrementing CSV `A-FAKE0001`…, a validation URL, a `Huella` in the XML, a real PNG QR); GetQrCode → the same PNG (as the real service, `Return` is the image itself); GetFilteredList → an empty list.
- **Reply builders** shaped like the captured fixtures in `test_verifactu_response`: `acceptedReply(csv)`, `rejectedReply(code, text)`, `queryReply(invoiceId, isoDate, total, csv)`.

### `ModalAutoCloser`

Headless there is nobody to click "Aceptar", so a single `QMessageBox` would block the suite until the CTest timeout. This helper closes any message box shortly after it opens and **records its title and text**; a Yes/No confirmation is answered **Yes**, like an operator who agrees. A scenario asserts with `sawMessageContaining("Trimestre bloqueado")`.

### `ModalDriver`

Operates the app's own modal dialogs. A screen that opens a dialog with `exec()` (Recogida's pay-all → PayDialog, the Verifactu dialog, the AEAT comparison dialog) blocks the test until the dialog closes, so the scenario registers the step beforehand: `expect(ModalDriver::named("aeatReconcileDialog"), [](QWidget *w) { ... })` (or `ModalDriver::ofType<PayDialog>()`) runs the callback once on the first visible window that matches. It reads the dialog, presses its buttons or closes it. Message boxes stay with `ModalAutoCloser`.

### `testschema.h`

`TestSchema::create` builds the shop tables on a throwaway DB: `ingresos` with its original pre-Verifactu columns, then the app's own `migrateDatabase()`, so a column added by a migration reaches every suite without being copied by hand. Used by `e2efixture.h`, `test_e2e_verifactu` and `test_sql_lite` (whose `test_ingresosSchema_matchesColumnIndices` checks the result against `INGRESOS_COL_*`).

### `e2efixture.h`

Shared setup for `test_e2e_app` (namespace `E2e`): `createSchema` (via `testschema.h`), `configureSettings` (the throwaway settings above), `clearTables`, `exec` / `scalar`, and the seeders `seedGarment` (unpaid, `SIN COBRAR`) and `seedSentGarment` (paid, `ENVIADA`, with a CSV).

---

## `test_e2e_verifactu` (phase 1)

Drives the real `PayDialog` as the Cobrar button does (load the ticket, untick garments for a partial payment, set the payment date, invoke Cobrar, wait for it to finish), with the real `VerifactuIntegration` / `VerifactuManager` and network stack and the real `sql_lite` writes. `QTEST_MAIN` under offscreen; about 9 s.

| Scenario | Flow | Checks |
|----------|------|--------|
| `test_payment_acceptedByAeat` | Cobrar → AEAT accepts | One Create with invoice id = bare ticket number and the service key; both garments paid, `ENVIADA`, CSV, `Huella`, payment date; no pop-up |
| `test_partialPayments_eachEventIsItsOwnInvoice` | Pay one garment, later the other | Two invoices `200` and `200-1`; the second garment has seq 1 |
| `test_noReplyInTime_staysPendiente_thenReconciledFromAeat` | AEAT replies after 7 s (PayDialog waits 5 s) | Payment kept `PENDIENTE`; the retry is answered "already exists" (`verifactuErrorIsDuplicate`); querying AEAT returns the accepted record, which matches and is adopted → `ENVIADA` with its CSV |
| `test_aeatRejection_marksError` | AEAT rejects | Sale stays paid, estado `ERROR` with the reason |
| `test_paymentIntoClosedQuarter_refusedAndNothingSent` | Payment dated in a closed month | Refused with the "Trimestre bloqueado" warning; no request sent; garment unpaid |
| `test_cancelAfterClose_regularisedInLaterQuarter_remainderStillChargeable` | Pay one garment in Q1 → close Q1 → cancel at AEAT in Q2 → charge the rest in Q2 | Cancel request sent; paid garment `ANULADA` with `fecha_anulacion`; unpaid one still `SIN COBRAR`; Q1 figures unchanged; the rest invoiced as `600-1`; Q2 nets the regularisation and counts the ticket once |

The last scenario found a real bug on its first run: the 10.12 ticket count let an earlier period's cancelled payment cancel out a new sale (fixed in `Contabilidad::netTicketCount`).

### Adding a scenario

1. Seed rows with `seedGarment(nRecibo, hash, importe)` (or `exec()` for anything else).
2. Script the AEAT with `m_server.enqueue(...)` when the default "accepted" is not what you need.
3. Drive the flow (`payThroughDialog(...)`, or call `m_verifactu` directly for cancel / query / retry, as the app dialogs do).
4. Assert on the DB (`scalar(...)`), on `m_server.requestsTo(...)`, and on `m_popups` messages.
5. Keep it well under the 120 s suite limit — prefer short delays.

---

## `test_e2e_app` (phase 2)

The application's windows live in the `laideal_app` static library (`src/app/CMakeLists.txt`; the executable is only `main.cpp` + resources), so the suite links and drives them the way an operator would: fill the widgets (found by object name), press the button or invoke the slot it is wired to, then assert on the DB, on what reached the fake AEAT and on the pop-ups. MainWindow opens `DB_PATH` (set with `setDbPath` to the throwaway file) as the default connection, so the test reads the same file through its own connection. The dialogs are built with a `VerifactuIntegration` exactly as the menu actions do. Recogida is driven through its search field and a row selection (`selectRow`), then its buttons, with `ModalDriver` operating the dialogs they open. `QTEST_MAIN` under offscreen; about 15 s (the late-reply scenario waits out PayDialog's 5 s).

| Scenario | Flow | Checks |
|----------|------|--------|
| `test_mainWindow_saveUnpaidThenPaidTicket` | Type client + garment (price looked up from `prendas`), Save; then a paid one | Unpaid: `SIN COBRAR`, importe, new client stored, no request, form reset to the next number. Paid: one Create with the ticket number, row `SI` / `fecha_pago` today / `ENVIADA` / CSV (the 3 s print-after-submit wait and the reqId correlation) |
| `test_mainWindow_saveWithoutClient_refused` | Save with no client | Refusal message, nothing stored |
| `test_voidGarments_voidsOnlyTheTickedGarment` | Anular prendas, tick one of two, confirm | That garment `Anulado` / `NO` / `ANULADA`, `fecha_anulacion` today, Pago and Recogida empty; the other untouched; no AEAT request; the voided row can no longer be ticked |
| `test_cancelInvoice_markedAnulada` | Anular factura on a ticket received 20 days ago, paid 10 days ago (`800`) and a later partial payment today (`800-1`) → AEAT accepts both | Each Cancel carries its InvoiceID and its own payment date (not the reception date); `ANULADA` + `fecha_anulacion` today; AEAT's cancellation record stored in `verifactu_cancel_xml` |
| `test_cancelInvoice_olderDataDates` | Anular factura on older data: a seq-0 invoice paid on 28-01 and 05-03 (AEAT rejects the first attempt); a ticket with an unreadable payment date; an invoice AEAT rejects twice | The first Cancel names 28-01 (earliest date, not the smaller text), the retry after the rejection names the reception date and is accepted, both rows `ANULADA`; the unreadable one is refused locally and nothing is sent; when both attempts fail the dialog shows both AEAT answers and the invoice stays `ENVIADA` |
| `test_cancelInvoice_refusedWhileCurrentQuarterClosed` | Same with today's quarter locked | Refused before any request; row still `ENVIADA` |
| `test_cancelAndRectify_offeredOnlyForSentInvoices` | Anular factura on a `PENDIENTE` invoice and an unpaid ticket; Rectificar on the unpaid one | Anular button disabled on `PENDIENTE`; the unpaid ticket has no invoice to cancel and Rectificar refuses it; no request |
| `test_rectifySubstitution_dateRuleThenRectified` | Rectificar por sustitución | A date before the payment and a date in a closed quarter are refused (nothing inserted); then the new ticket `701` is sent, `ENVIADA`, rectifies `700`; the original is `RECTIFICADA` |
| `test_startupRecovery_retryPendingSubmission` | MainWindow opens with a `PENDIENTE` payment + an unpaid ticket | About 4 s later Envíos pendientes lists only the payment; Reintentar re-submits it under the same InvoiceID → `ENVIADA`; the dialog closes after its last row |
| `test_reprintPaymentEvent_scopedRowsAndQrGating` | Imprimir for event `600-1`, then `600` | Only that event's paid garments are loaded (the unpaid seq-0 garment is not); GetQrCode carries `600-1` and its payment date; once the event is `ANULADA` no QR is requested |
| `test_recogida_lateReplyAdoptedAfterPayDialogGivesUp` | Recogida → pay-all → Cobrar; AEAT answers after 7 s | PayDialog gives up at 5 s and the payment is kept `SI` / `PENDIENTE`; Recogida takes over the in-flight request and the late reply makes it `ENVIADA` with the CSV; the status bar says the factura with QR can be printed; one Create only |
| `test_recogida_duplicateRetry_comparisonDialogAdoptsAeatCsv` | Verifactu dialog on an `ERROR` row → Reintentar; AEAT answers "duplicado", then the query returns the matching record | Reintentar and Consultar offered; the comparison dialog opens by itself, says the data match, shows AEAT's CSV, Actualizar enabled; after it the row is `ENVIADA` with AEAT's CSV and the confirmation shows |
| `test_recogida_aeatQuery_noAdoptionUnlessItMatches` | Consultar en AEAT: not found, different amount, row already `ENVIADA`; Verifactu dialog on an unpaid row | Actualizar disabled in all three ("no ha devuelto ninguna factura", "NO coinciden", "solo informativa"); `ENVIADA` row offers no Reintentar; unpaid row offers neither button; nothing re-submitted |
| `test_aeatExport_oneRegistroPerPaymentEvent` | Exportar registros AEAT as the menu runs it (records + XML writer) for March: a two-garment payment, a later partial payment, an unpaid garment, an invoice known only by its CSV, a January invoice cancelled in March (cancellation record stored), a March invoice cancelled in April | Four `<Registro>` for the invoices issued in March (`1600` with the event total 14.50, `1600-1`, `1700` marked `sinPayload`, `1900` with its later `fechaAnulacion`) and one `<Anulacion>` for `1800` with AEAT's cancellation record; the January invoice itself is not repeated; each payload inlined once; the document parses |
| `test_aeatExport_awkwardStoredData` | Exportar registros AEAT over a DOCTYPE payload, an undeclared-prefix payload, an invoice registered under its reception date and cancelled, a cancellation without a date and one with an unreadable date, an undated invoice | The bad payloads are written as text (`payloadComoTexto`) and the file parses; `fechaExpedicion` from the first date of the payload on both `<Registro>` and `<Anulacion>`; both undated cancellations follow their invoice with `sinFecha`; the undated invoice is counted |
| `test_recogida_splitPaidGarment_staysInItsInvoice` | Recogida → Separar prendas (1 of 3) on a paid garment of invoice `2500-1` | Both rows `ENVIADA`, same CSV, seq 1 and id; 3.33 + 6.67 = the original 10.00 |
| `test_recogida_m2SizeStoredInCents` | Recogida: type 0, then 2,99 as the size of an unpaid 9.50 m2 garment | Size 0 is not priced; 2,99 is stored as size 2.99, importe 28.41 (not 28.405), shown as 28.41 |
| `test_recogida_replyForSettledInvoiceIgnored` | Reintentar on an `ERROR` row; the invoice is registered meanwhile; AEAT then answers "duplicate" | Nothing rewritten, status bar "...ignorada: la factura ya estaba registrada", no AEAT query |
| `test_addGarmentPaid_submittedAtOnce` | Herramientas → Añadir nuevas prendas on an unpaid ticket, adding a garment as paid | Submitted at once as the ticket's first invoice (`2000`, today, 7.00), row `ENVIADA` in seq 0; the ticket's unpaid garment stays `SIN COBRAR` |
| `test_addGarment_retypedTicketNumberRefused` | Añadir nuevas prendas: search an unpaid ticket, retype an already sent one, save as paid | Refused ("No se ha buscado…"); nothing inserted into the sent ticket, no Create, its CSV intact |
| `test_contabilidad_generateLockThenRevert` | Contabilidad Trimestral Q1 with Bloquear; Comprobar bloqueo; again with Incluir detalle; Cerrar; then Revertir and Comprobar bloqueo | The summary PDF (no detail tables) is written under `reportsRoot/Contabilidad` and the rows locked; the form stays open; the check says locked; the detailed report is a second file `_detalle.pdf` with the detail tables; Cerrar closes the form; revert unlocks the rows and the check says not locked |

Each scenario was checked against a mutated build (quarter guard removed, date rule removed, retry signal not emitted, the `pagado='SI'` filter dropped from `getTicketInfo`, the Anular button enabled for every estado, Recogida not taking over PayDialog's request, no query after a duplicate, Actualizar enabled on any found record, Consultar shown on unpaid rows); each mutation made its scenario fail.

**Not automated**: the real printer, PDF and screen rendering, menu wiring and the Listado lock are in [smoke_test.md](smoke_test.md), which never contacts AEAT. The real AEAT itself is checked once, with a single real ticket after the production switch.
