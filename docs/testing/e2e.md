# End-to-end test bench

Runs the real application objects together — not one module in isolation — against a seeded throwaway database and a **fake Verifactu server**, so whole flows (ticket save / payment → AEAT submission → reply → estado / QR → cancellation / rectification / recovery → Contabilidad) are checked on every CI run without ever reaching the real AEAT. Conventions shared with the other suites are in [README.md](README.md).

Two suites: `test_e2e_verifactu` (phase 1: PayDialog and the Verifactu client) and `test_e2e_app` (phase 2: MainWindow, the app dialogs, Imprimir and the Contabilidad form at screen level).

---

## Safety rules

The bench must never reach the real AEAT nor change the shop's database or settings:

- **Endpoint**: the test-only seam `VerifactuConfig::setEndpointOverride(baseUrl)` points every Verifactu call (Create, Cancel, GetFilteredList, GetQrCode) at the local fake server. Only code linked into a test can call it; nothing in the settings, environment or registry reaches it, so a shop install can never divert real invoices. The suite **refuses to start** unless the resolved endpoint is `http://127.0.0.1:…`.
- **Database**: a throwaway SQLite file in a `QTemporaryDir`, created and seeded by the test.
- **Settings**: a throwaway file via `AppSettings::loadFrom`, so every save goes there and `~/.laideal_settings.json` is never read or written. Printing is disabled (`enablePrinting = false`); the ticket is still built. `test_e2e_app` also turns off the startup update check and backup, points the reports root at the temp dir, and sets `ReportHtml::setOpenGeneratedReports(false)` so a generated PDF is not opened in a viewer.

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

Operates the app's own modal dialogs. A screen that opens a dialog with `exec()` (Recogida's Cobrar… → PayDialog, the Verifactu dialog, the AEAT comparison dialog) blocks the test until the dialog closes, so the scenario registers the step beforehand: `expect(ModalDriver::named("aeatReconcileDialog"), [](QWidget *w) { ... })` (or `ModalDriver::ofType<PayDialog>()`) runs the callback once on the first visible window that matches. It reads the dialog, presses its buttons or closes it. Message boxes stay with `ModalAutoCloser`.

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
| `test_gateway_http500_staysPendiente`, `_http401_`, `_malformedJson_`, `_droppedConnection_`, `_successWithoutCsv_` | The gateway answers 500, 401, malformed JSON, a dropped connection, or "success" without CSV | The rows stay paid and `PENDIENTE` with their InvoiceID, no CSV, never `ERROR` (the outcome is unknown) |
| `test_gatewayRectificativaRefusedInProduction` | A rectificativa with the gateway in PRODUCTION | Refused (it carries no RectificationItems); nothing sent |
| `test_testConnectionReportsARefusedKey` | Probar conexión with a refused key (401, or a key error in the body) | Reported as a refused key, not "Conexión correcta" |
| `test_gateway_stalledReply_endsAsNetworkErrorAtTheTransferTimeout` | The gateway never answers | NETWORK_ERROR at the 30 s transfer timeout (about 30 s) |
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
| `test_mainWindow_saveWithoutClient_refused` | (refusal read from the result panel, no pop-up) Save with no client | Refusal message, nothing stored |
| `test_mainWindow_refusedInvoicesStayInView` | A paid invoice in ERROR, then a 2020 one refused for its old date, with the recovery floor at 2021; the red count clicked; Abrir en Recogida | The count shows only the current refusal and is a link; Facturas rechazadas lists that ticket with "9,00 €" and AEAT's reason; Recogida opens on it with the refused row selected and Verifactu… enabled; once resent the count disappears |
| `test_mainWindow_entryFlowAndMessages` | MainWindow: garment on row 1 before its quantity; a hand-typed `3,00`; an unmeasured Jarapa (m2) saved alone, then the same marked paid; a quantity left in an empty slot; a ticket dated in a closed quarter; the Recogida quick button | Quantity 1 and 3.50 on that row only, total read-only and following the typed price; the Jarapa saved at 0.00 and summarised in the result panel; paid, it is refused with nothing saved or sent; the empty slot is not stored; the closed quarter refused there with nothing saved; Recogida opens; no pop-up |
| `test_voidGarments_voidsOnlyTheTickedGarment` | Anular prendas, tick one of two, confirm | That garment `Anulado` / `NO` / `ANULADA`, `fecha_anulacion` today, Pago and Recogida empty; the other untouched; no AEAT request; the voided row can no longer be ticked |
| `test_recogida_addGarmentFromTheWindow` | Recogida → select a garment → Añadir prendas… → add a Camisa as paid → Cerrar | The window opens with ticket 920 found; the garment is sent to AEAT once as `920` and ends ENVIADA; Recogida reports "1 prenda(s) añadidas" (kept when the AEAT reply refreshes the table); the window deletes itself; the menu bar is Archivo / Tickets / Facturas / Contabilidad / Ver / Ayuda with each action in place, no Herramientas menu, Revertir contabilidad… without a shortcut and Alt+A / Alt+T on Anular / Rectificar |
| `test_recogida_voidGarmentsFromTheWindow` | Recogida → select a garment → Anular prendas… → tick it → Anular seleccionadas | The dialog opens on ticket 910; the garment is `Anulado` with today's `fecha_anulacion`, Recogida reports it and shows that date under Anulación ("-" before); MainWindow has no `actionAnular_prendas` any more |
| `test_cancelInvoice_markedAnulada` | Anular factura on a ticket received 20 days ago, paid 10 days ago (`800`) and a later partial payment today (`800-1`) → AEAT accepts both | Each Cancel carries its InvoiceID and its own payment date (not the reception date); `ANULADA` + `fecha_anulacion` today; AEAT's cancellation record stored in `verifactu_cancel_xml` |
| `test_cancelInvoice_olderDataDates` | Anular factura on older data: a seq-0 invoice paid on 28-01 and 05-03 (AEAT rejects the first attempt); a ticket with an unreadable payment date; an invoice AEAT rejects twice | The first Cancel names 28-01 (earliest date, not the smaller text), the retry after the rejection names the reception date and is accepted, both rows `ANULADA`; the unreadable one is refused locally and nothing is sent; when both attempts fail the dialog shows both AEAT answers and the invoice stays `ENVIADA` |
| `test_cancelInvoice_refusedWhileCurrentQuarterClosed` | Same with today's quarter locked | Refused before any request; row still `ENVIADA` |
| `test_cancelAndRectify_offeredOnlyForSentInvoices` | Anular factura on a `PENDIENTE` invoice and an unpaid ticket; Rectificar on the unpaid one | Anular button disabled on `PENDIENTE`; the unpaid ticket has no invoice to cancel and Rectificar refuses it; no request |
| `test_rectifySubstitution_dateRuleThenRectified` | Rectificar por sustitución | A date before the payment and a date in a closed quarter are refused (nothing inserted); then the new ticket `701` is sent, `ENVIADA`, rectifies `700`; the original is `RECTIFICADA` |
| `test_startupRecovery_retryPendingSubmission` | MainWindow opens with a `PENDIENTE` payment + an unpaid ticket | About 4 s later Envíos pendientes lists only the payment; Reintentar re-submits it under the same InvoiceID → `ENVIADA`; the dialog closes after its last row |
| `test_reprintPaymentEvent_scopedRowsAndQrGating` | Imprimir for event `600-1`, then `600` | Only that event's paid garments are loaded (the unpaid seq-0 garment is not); GetQrCode carries `600-1` and its payment date; once the event is `ANULADA` no QR is requested |
| `test_imprimirDialog_reciboFacturaCompletaAndPartialPayments` | Imprimir dialog: an unknown number; a recibo; a factura completa with DNI / address typed; a ticket with two partial-payment invoices, choosing `651-1` | Each outcome in the result panel (printing off is stated); the DNI and address are on the rendered ticket; the first press lists the invoices (2 + all), the second prints the chosen one; no pop-up |
| `test_imprimirDialog_qrNotReusedForTheNextTicket` | Imprimir kept open: factura of a sent ticket, then of a voided one | The first carries its QR; the second has none ("Sin código QR") and no second QR request - the previous QR is never reused |
| `test_enterInNumberFieldRunsOneAction` | Enter in the number field of Imprimir (factura), Añadir prendas (after a search, with a garment typed; also the keypad Enter) and Anular prendas; an auto-repeated Enter in Imprimir | One factura printed (one QR request); Añadir prendas only searches again ("encontrado", nothing inserted); Anular prendas only searches again; the auto-repeat prints nothing |
| `test_enterInCancelAndRectifyDialogs` | Enter in the ticket field of Anular / Rectificar factura; then Enter in Rectificar's amount after the send button had the focus | Each only searches; the amount is not reset and nothing is sent to AEAT |
| `test_mainWindow_failedInsertStopsTheSave` | A paid two-garment ticket whose second insert fails (test trigger) | No row kept (all-or-none), nothing sent to AEAT nor printed, "No se pudo guardar" in the panel |
| `test_recogida_refusedWritesAreReported` | Recogida: Separar on a row in a quarter closed by Contabilidad; a negative typed price | Each refused in the result panel (no "separadas" success); the rows unchanged |
| `test_payDialog_storesTheAmountItSends` | Cobrar on a row stored with three decimals (12.345) | Sent to AEAT as 12.35 and stored as 12.35 - the books match the invoice |
| `test_payDialog_storesThePaymentBeforeSubmitting` | Cobrar on two garments with a slow AEAT reply | Right after Cobrar, before AEAT answers, both rows are paid + `PENDIENTE` with InvoiceID 955; after the reply both are `ENVIADA`; one Create |
| `test_payDialog_refusesZeroTotalAndNoIntegration` | Cobrar on a 0,00 garment; Cobrar with no Verifactu integration | Both refused in the panel; nothing paid, nothing sent |
| `test_payDialog_garmentPaidMeanwhileStopsTheCharge` | Cobrar on two garments; one is paid from elsewhere after the dialog opened | Refused ("no se ha enviado nada"); the other stays unpaid `SIN COBRAR`; nothing sent |
| `test_payDialog_secondPressWhileWaitingSendsNothingMore` | Cobrar pressed twice while AEAT answers | One Create; the rows end `ENVIADA` (Cobrar is ignored while a reply is pending) |
| `test_recogida_searchModes` | Recogida search by Anulación date (`/` and `-`), Recepción vs Anulación on one date, accented / case-insensitive name, not found, unknown phone | The right rows, and the messages in the panel |
| `test_payDialog_refusesUnmeasuredM2Garment` | Cobrar on an unpaid Jarapa (m2) with no size | Refused in the dialog ("no tiene tamaño"); still unpaid; nothing sent to AEAT |
| `test_recogida_lateReplyAdoptedAfterPayDialogGivesUp` | Recogida → Cobrar… → Cobrar; AEAT answers after 7 s | PayDialog gives up at 5 s and the payment is kept `SI` / `PENDIENTE`; Recogida takes over the in-flight request and the late reply makes it `ENVIADA` with the CSV; the status bar says the factura with QR can be printed; one Create only |
| `test_recogida_duplicateRetry_comparisonDialogAdoptsAeatCsv` | Verifactu dialog on an `ERROR` row → Reintentar; AEAT answers "duplicado", then the query returns the matching record | Reintentar and Consultar offered; the comparison dialog opens by itself, says the data match, shows AEAT's CSV, Actualizar enabled; after it the row is `ENVIADA` with AEAT's CSV and the confirmation shows |
| `test_recogida_aeatQuery_noAdoptionUnlessItMatches` | Consultar en AEAT: not found, different amount, row already `ENVIADA`; Verifactu dialog on an unpaid row | Actualizar disabled in all three ("no ha devuelto ninguna factura", "NO coinciden", "solo informativa"); `ENVIADA` row offers no Reintentar; unpaid row offers neither button; nothing re-submitted |
| `test_aeatExport_oneRegistroPerPaymentEvent` | Exportar registros AEAT as the menu runs it (records + XML writer) for March: a two-garment payment, a later partial payment, an unpaid garment, an invoice known only by its CSV, a January invoice cancelled in March (cancellation record stored), a March invoice cancelled in April | Four `<Registro>` for the invoices issued in March (`1600` with the event total 14.50, `1600-1`, `1700` marked `sinPayload`, `1900` with its later `fechaAnulacion`) and one `<Anulacion>` for `1800` with AEAT's cancellation record; the January invoice itself is not repeated; each payload inlined once; the document parses |
| `test_aeatExportDialog_exportsAndReportsInWindow` | Facturas → Exportar registros AEAT window: a reversed range, then March 2026 | The reversed range is refused in the result panel; the file name follows the dates (`AEAT/aeat_registros_20260301_20260331.xml`); "1 registros exportados" with the file linked; the file exists; no pop-up |
| `test_aeatExport_awkwardStoredData` | Exportar registros AEAT over a DOCTYPE payload, an undeclared-prefix payload, an invoice registered under its reception date and cancelled, a cancellation without a date and one with an unreadable date, an undated invoice | The bad payloads are written as text (`payloadComoTexto`) and the file parses; `fechaExpedicion` from the first date of the payload on both `<Registro>` and `<Anulacion>`; both undated cancellations follow their invoice with `sinFecha`; the undated invoice is counted |
| `test_recogida_splitPaidGarment_staysInItsInvoice` | Recogida → Separar 1 (the count field offers 1-2 of 3) on a paid garment of invoice `2500-1` | Reported in the result panel; both rows `ENVIADA`, same CSV, seq 1 and id; 3.33 + 6.67 = the original 10.00 |
| `test_recogida_m2SizeStoredInCents` | Recogida: type 0, then 2,99 as the size of an unpaid 9.50 m2 garment | Size 0 is not priced; 2,99 is stored as size 2.99, importe 28.41 (not 28.405), shown as 28.41 |
| `test_recogida_replyForSettledInvoiceIgnored` | Reintentar on an `ERROR` row; the invoice is registered meanwhile; AEAT then answers "duplicate" | Nothing rewritten, status bar "...ignorada: la factura ya estaba registrada", no AEAT query |
| `test_addGarmentPaid_submittedAtOnce` | Tickets → Añadir nuevas prendas on an unpaid ticket, adding a garment as paid | Submitted at once as the ticket's first invoice (`2000`, today, 7.00), row `ENVIADA` in seq 0; the ticket's unpaid garment stays `SIN COBRAR`; the result panel reports it and the form is cleared, no pop-up |
| `test_addGarment_retypedTicketNumberRefused` | Añadir nuevas prendas: search an unpaid ticket, retype an already sent one, save as paid | Refused in the result panel ("No se ha buscado…"); searching the sent ticket itself is refused before the form opens; nothing inserted into the sent ticket, no Create, its CSV intact |
| `test_addGarment_refusesAGarmentNotInTheList` | Añadir nuevas prendas on a found ticket: a free-typed garment, then quantity 0 | "no está en la lista" and "al menos 1" in the result panel; nothing inserted, no pop-up |
| `test_paymentDatesBoundedByReceptionAndToday` | PayDialog and AddGarment (and the main form) on a ticket received 5 days ago | The payment date editors are bounded by today and the reception date; the main form's reception date by today |
| `test_facturas_saveAndRefusals` | Formulario de facturas: 121,00 at 21 %, save; an unknown supplier; a date in a closed quarter | Base 100.00 and IVA 21.00 shown as typed; saved as `121.00` / 21 and reported in the result panel, form cleared; the two refusals are reported there with nothing written; no pop-up |
| `test_genListadoGastos_pdfAndEmptyYear` | Listado de gastos → Generar PDF for 2026 grouped by supplier; then only closed quarters of 2025 | Only 2026 rows, suppliers in order with IMPORTE TOTAL, PDF in Listados/Gastos linked in the result panel; the empty configuration is reported there; no pop-up |
| `test_listado_buttonsAndMessagesInWindow` | Listado de gastos: buttons, Añadir fila from the menu, double-click on a row of a closed quarter, then its edit flags and Eliminar fila; Listado de clientes: Añadir fila through Nuevo cliente (first without a name) | Gastos offers no Añadir but a PDF; both messages in the result panel; the locked row is not editable and is not deleted ("Fila bloqueada"); the client form refuses the empty name in its panel, then saves the client; "Cliente añadido"; no pop-up |
| `test_listado_gastosDateNeverMovesIntoAClosedQuarter` | Listado de gastos: an open Q2 row's date typed into closed Q1, then as 2026-05-11, then a valid Q2 date | The first two refused in the panel ("Trimestre bloqueado", "Fecha no válida") with the DB unchanged; the valid one stored and shown - the table still reads the DB |
| `test_contabilidad_generateLockThenRevert` | Contabilidad Trimestral Q1 with Bloquear; Comprobar bloqueo; again with Incluir el detalle; Cerrar; then Revertir, Comprobar bloqueo and Revertir again | The summary PDF (no detail tables) is written under `reportsRoot/Contabilidad` and the quarter closed (one `accounting_closures` row, origin `contabilidad`, with the PDF's name; regenerating adds none); every outcome is in the result panel with the PDF's name and no pop-up appears; the form stays open; the check says locked with the close date; the detailed report is a second file `_detalle.pdf` with the detail tables; Cerrar closes the form; the revert form has its own title and no detail option, reopens the quarter (the closure is kept with its `reopened_at`), the check says not locked with the revert date, a second revert says there is nothing to revert |
| `test_contabilidad_unwrittenReportDoesNotLock` | Contabilidad Trimestral Q2 with Bloquear, its PDF path taken by a folder | "No se pudo guardar el informe" in the panel, no "realizada", no closure is written |
| `test_contabilidad_quarterWithOnlyACancellation` | Contabilidad Trimestral Q3 (Bloquear ticked) whose only movement is the cancellation of a Q2 invoice; Comprobar bloqueo | The report is written with the "Anulaciones / rectificaciones del periodo" line and the 12.10; "realizada" in the panel and Q3 closed like any other quarter, Q2 still open; Comprobar bloqueo says Q3 is locked with its close date; no pop-up |

Each scenario was checked against a mutated build (quarter guard removed, date rule removed, retry signal not emitted, the `pagado='SI'` filter dropped from `getTicketInfo`, the Anular button enabled for every estado, Recogida not taking over PayDialog's request, no query after a duplicate, Actualizar enabled on any found record, Consultar shown on unpaid rows); each mutation made its scenario fail.

**Not in this bench**: menu wiring, the printer transport and the installed app are covered by the UI smoke test ([ui_smoke.md](ui_smoke.md)); the Listado lock is covered by both (here by `test_listado_buttonsAndMessagesInWindow` and `test_listado_gastosDateNeverMovesIntoAClosedQuarter`); the physical printer, the shop's real schema and how screens look by the manual [smoke_test.md](smoke_test.md). The ticket bytes themselves (QR raster, leyenda) are asserted here through `Imprimir::ticketBytes()`. The real AEAT itself is checked once, with a single real ticket after the production switch.

### More `test_e2e_app` scenarios

One line each; the test body has the exact assertions.

**Start-up and Configuración**

| Test | Proves |
|---|---|
| `test_mainWindow_asksForTheCheckOnceAfterAnUpdate` | After an update the first start asks once for the post-update check; later starts do not |
| `test_mainWindow_asksForTheCheckAfterAnUpdateFrom12_0` | From 12.0 (which did not record the started version) the pre-migration copy's version tells the update; with neither (a first installation) nothing is asked |
| `test_mainWindow_preMigrationCopyWithBackupsOff` | A new version copies the database before migrating it, also with automatic backups off |
| `test_mainWindow_reinitialisesOneBackend` | Aceptar in Configuración re-initialises Verifactu with one integration and one backend (two direct clients would both send the outbox) |
| `test_mainWindow_showsThePreProductionEnvironment` | The status bar says when the direct client registers in AEAT's test environment |

**Direct connection in the main window, Recogida and Cobrar**

| Test | Proves |
|---|---|
| `test_mainWindow_directSavePrintsAtOnce` | A paid ticket's record is generated and chained at once, so the factura with QR prints without waiting for AEAT (the gateway waits up to 3 s); rows stay PENDIENTE until AEAT answers |
| `test_mainWindow_settlesAnOutcomeNobodyWaitedFor` | A record an earlier session left unanswered goes out at start and its rows become ENVIADA |
| `test_recogida_directCobrarSettledByRecogida` | Cobrar over the direct connection prints at once; Recogida settles the rows when AEAT accepts and asks for no reprint |
| `test_payDialog_directPrintsFacturaBeforeAeatAnswers` | Cobrar prints the factura with the QR drawn from the invoice and the VERI*FACTU legend; rows paid and PENDIENTE; no QR before the record exists, nor with the gateway |
| `test_recogida_lateRejectionShownInThePanel` | A refusal that arrives after the customer left stays in the result panel and says how to resend it |
| `test_recogida_searchReturnsEveryRowBeyondTheFirstBatch` | A long Recogida search lists every row (the model fetches 256 at a time) |

**Chain gate, numbering and not-configured**

| Test | Proves |
|---|---|
| `test_mainWindow_noInvoicingUntilTheChainIsChecked` | The first check cannot reach AEAT: no ticket is saved, paid or not; the refused attempt checks again and the number continues after AEAT's |
| `test_mainWindow_noSaveWhileTheFirstCheckRuns` | While the first check still asks AEAT no ticket is saved |
| `test_gateOnlyWhileTheChainIsUnchecked` | Recogida's Añadir is refused like Cobrar while the chain is unchecked; an ordinary start never blocks saving |
| `test_payDialog_noInvoicingUntilTheChainIsChecked` | Cobrar is refused like a paid save while the chain check has failed |
| `test_mainWindow_numberRefreshedAfterAnyChainSync` | Any sync that succeeds after a failed first check moves the number on screen past AEAT's highest |
| `test_mainWindow_syncDuringASaveKeepsItsNumber` | A sync ending while a paid save waits never changes the number on screen |
| `test_mainWindow_numbersAboveAeatAfterARestore` | A restored (older) database continues above the numbers AEAT holds for our system |
| `test_mainWindow_preProductionNumbersFollowTheDatabase` | In pre-production AEAT's test numbers never move the shop's numbering and no restore warning shows |
| `test_noInvoiceWhileVerifactuIsNotConfigured` | Nothing is charged (save, Cobrar, Añadir with a paid garment) while Verifactu is not configured; an unpaid ticket still saves |
| `test_selfTestSharesTheShopChainOnlyInPreProduction` | The self-test shares the shop's chain only while the direct client is in pre-production |
| `test_pendingRecoverySkipsAnInvoiceBeingSent` | The start-up recovery does not offer an invoice still being sent |
| `test_pendingRetryKeepsTheRowWhenNothingWasSent` | The start-up Reintentar keeps the row listed with the reason when nothing was sent |

**Anular factura**

| Test | Proves |
|---|---|
| `test_cancelInvoice_asksForConfirmationFirst` | The first press only says what will be cancelled ("Confirmar anulación"); a new search drops that; the second press sends |
| `test_cancelInvoice_confirmButtonFitsItsText` | The row button is wide enough for "Confirmar anulación" |
| `test_cancelInvoice_refusedWhileARectificativaIsSent` | An invoice with a standing rectificativa is not cancelled until that one is |
| `test_cancelInvoice_replyAppliesToTheTicketItWasSentFor` | While a cancellation waits no other ticket can be searched; the reply marks the invoice it was sent for |
| `test_cancelDirectQueuedIsPending` | A cancellation the direct client stored but could not send yet is pending, not an error, and the button stays off |
| `test_dialogsCannotCloseWhileAeatAnswers` | Esc / the X is refused in Cobrar, Anular and Rectificar while AEAT answers |

**Rectificar factura**

| Test | Proves |
|---|---|
| `test_rectifyNamesTheStoredRecordsInvoice` | The R5 names the corrected invoice as its stored record has it |
| `test_rectifyInvoice_mixedPaidUnpaidTicketUsesThePaidInvoice` | Rectificar reads the paid seq-0 invoice; an unpaid garment neither hides it nor adds to the original amount |
| `test_rectifySubstitutionAfterADifferenceRefused` | A substitution after a difference is refused; the ticket is corrected by differences |
| `test_rectify_differenceCannotMakeTheTotalNegative` | A difference never takes the invoice to 0 or below (that is Anular) |
| `test_rectify_placeholderNotStoredSendsNothing` | A rectificativa whose row cannot be stored is not sent |
| `test_rectify_replyAppliesToTheTicketItWasSentFor` | While one waits no other ticket can be searched; the reply settles the row it was sent for |
| `test_rectifyDirectOnlyR5_noRowLeft` | With the direct client a non-R5 type is refused before a number is claimed, leaving no placeholder row |
| `test_rectifyDirectQueuedWithoutAnswer` | With no answer yet the rectificativa is stored and chained, PENDIENTE not ERROR, and the ticket is never offered a second one |
| `test_rectifyDirectWithoutRecordIsAnError` | Before the first chain sync no record exists, so the row is an error to redo, never a PENDIENTE row nothing would send |
| `test_rectifyGatewayClosedBeforeTheAnswer` | A gateway rectificativa whose dialog closed before the answer becomes an error, counted and redone |
| `test_rectifyGatewayPendingAnswerSendsNoSecond` | A gateway answer without the CSV (PENDING) never lets a second press send another rectificativa |
| `test_pendingRectificativaBlocksTheTicket` | While one waits for AEAT the ticket can be neither rectified nor cancelled again |
| `test_queuedRectificativaIsLeftToTheDirectClient` | A rectificativa whose record is pending at AEAT is never turned into an error |
| `test_noRedoOverAPendingRectificativa` | A rectificativa marked ERROR while its record is still pending is never redone |
| `test_mainWindow_orphanedPendingRectificativaBecomesAnError` | At start a PENDIENTE rectificativa with nothing to send it becomes an error to redo |
| `test_startSettlesARectificativaFromItsStoredAnswer` | At start an accepted but unsettled rectificativa is settled from its stored answer |
| `test_refusedRectificativaRedoneUnderItsNumber` | A refused rectificativa is redone under its own number (Subsanacion + RechazoPrevio), its row reused; Recogida's Reintentar refuses it |
| `test_refusedRectificativaInAClosedQuarter` | One counted in a quarter closed since is resent as it was (date and amount), its closed row unchanged |
| `test_refusedRectificativaWithUnreadableDateNotResent` | One whose date cannot be read is never rewritten nor resent |

**Printing and reports**

| Test | Proves |
|---|---|
| `test_printedQrSizeOfARealQr` | A real VERI*FACTU QR as printed: 30-40 mm, whole dots per module, quiet zone |
| `test_reprintNeverBlankNorMerged` | A reprint never prints a blank ticket, a QR from a previous invoice or an invoice AEAT never received (trailing space still loads; all-voided refused; no row selected picks the only event or asks) |
| `test_reprintUsesTheStoredVerificationUrl` | A reprint draws the QR from the stored verification URL: no request, never a URL rebuilt for today |
| `test_contabilidad_unfinishedQuarterIsNotClosed` | The quarter that has not ended is reported but never closed |
| `test_contabilidad_unreadableDataNeitherReportsNorCloses` | Unreadable data: no all-zero report and no close, even with Bloquear ticked |

## Direct AEAT connection - `test_e2e_aeat_direct`

`VerifactuIntegration` initialised with *Directa con la AEAT* (research branch), the real Cobrar window and the fake AEAT SOAP service (`tests/support/fakeaeatserver`, plain HTTP on 127.0.0.1; a safety check aborts unless the endpoint is local).

| Test | Scenario | Asserts |
|---|---|---|
| `test_paymentRegistered` | Cobrar on two garments | One record (InvoiceID, payment date, total 25.00); both rows ENVIADA with AEAT's CSV, the record's huella, its XML and the pre-production QR URL |
| `test_partialPaymentChained` | Two payment events | The second is `200-1`, chained after the first record |
| `test_productionOnlyOnPurpose` | The gateway's PRODUCCIÓN box on; then the direct client's own production setting | Pre-production with the box alone; production only with its own setting |
| `test_rejection` | AEAT refuses the record | The row is ERROR with AEAT's description |
| `test_reprintWithoutStoredUrlHasNoQr` | Reprint of an accepted row with no stored verification URL (an older gateway row) | Printed without QR: the direct client never rebuilds the URL for today's environment |
