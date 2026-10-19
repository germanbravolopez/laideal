# Review: imprimir (src/imprimir/) - before release 12.0

## Summary
- Health: reasonable. The dialog, scoping by `invoiceSeq`, the Anulado filter, QR gating and the leyenda are mostly sound and the happy paths are covered by 5 e2e tests plus UI smoke C1-C3.
- No dedicated unit suite exists: every behaviour is tested only through the heavy `test_e2e_app`, and several branches are not tested at all.
- Real bugs found: a blank recibo is printed when the typed number has surrounding spaces or all garments are voided (no empty-model guard); the unscoped factura reprint from Recogida mixes payment events and computes the QR amount over unpaid rows (QR of a non-existent invoice); a cancelled/rectified invoice event reprints as an ordinary FACTURA SIMPLIFICADA; the recibo "Nº" is the Verifactu invoice id.
- Docs have clear drift on QR resolution (no-CSV branch / `qrBeforeReply`), leyenda condition, QR sizing and Status API call.
- Recommended before 12.0: fix the F1-F4 items (small), add a `test_imprimir` suite.

## Findings

| # | Severity | Category | file:line | Finding | Proposal | Effort |
|---|----------|----------|-----------|---------|----------|--------|
| F1 ✅ FIXED 19-10-2026 | High | bug | imprimir.cpp:452-457, 119-135, 298-300, 311 | `onPrintClicked` validates the TRIMMED `ticket` but `getTicketInfo` binds `le_n_ticket->text()` UNTRIMMED. "650 " passes the existence check (LIKE with trimmed text) and then loads 0 rows. For a recibo nothing checks the empty model (only the factura path calls `checkAnyItemPaid`), so `buildTicket` prints a ticket with empty client/date, total 0,00 and reports "Recibo Nº 650 impreso". Same blank ticket when every garment of an existing ticket is Anulado (the existence check at :453 ignores estado, `getTicketInfo` excludes Anulado). Engine callers (PayDialog, MainWindow, Recogida) have the same exposure whenever the scoped query returns nothing. | In `getTicketInfo` use `le_n_ticket->text().trimmed()` (or write the trimmed text back); in `buildTicket` return early / set `m_ticketBytes.clear()` when `rowCount()==0` (printTicket already refuses empty bytes, :364); in `onPrintClicked` show "El recibo Nº X no tiene prendas imprimibles (todas anuladas)" after `getTicketInfo` for recibo too. | S |
| F2 ✅ FIXED 19-10-2026 | High | compliance | recog_prendas.cpp:900-934, imprimir.cpp:119-139, 225-231, 207 | Recogida "Imprimir factura" with no clicked row passes `invoiceSeq=-1` and `verifactuIntegration`. `getTicketInfo` then loads ALL rows of the ticket (all payment events, unpaid too). `buildTicket` prints only paid rows of every event as ONE factura, `displayInvoiceId()` takes the first non-empty id, and `resolveQrCode` sums IMPORTE of ALL loaded rows including UNPAID ones (:226-228). The QR/total (and the printed Nº) therefore describe an invoice AEAT never received (combined events) or an amount that includes unpaid garments. The Imprimir dialog avoids this with the events query (:477-533) but the engine does not. | Make the engine safe by itself: in `resolveQrCode` only sum rows with `pagado='SI'` (same filter as the printed lines); when `invoiceSeq<0` and the loaded paid rows span more than one `verifactu_invoice_seq`, return no QR (or resolve seq automatically as `onPrintClicked` does). Better: move the "events" logic into a method (`resolveInvoiceEvents()`) used by dialog and Recogida. | M |
| F3 | Medium | compliance | imprimir.cpp:477-533, 538-545, 565-570 | A cancelled (ANULADA) or rectified (RECTIFICADA) invoice event is listed as a printable event and is printed as a normal "FACTURA SIMPLIFICADA" (e2e `qrNotReusedForTheNextTicket` confirms it prints, only "Sin código QR..." in the panel). The paper has no marker and the panel text says the invoice "no está confirmada", which is wrong for a cancelled one. Printing a cancelled invoice as a valid document is a mis-issue risk. | Per event read the verifactu_estado; for ANULADA/RECTIFICADA refuse (or print only with an explicit "ANULADA" legend, or after confirmation). Panel message distinct from "pending". | M |
| F4 | Medium | bug | imprimir.cpp:297, ticketrenderer.cpp:31 | The recibo prints `Nº: displayInvoiceId()`: the first non-empty `verifactu_invoice_id` of an UNSCOPED row set. For a ticket with partial payments the recibo can show "651-1" instead of the ticket number 651 (depends on rowid order); a recibo is a claim ticket and the customer / Recogida search by n_recibo. | `d.invoiceId = isRecibo ? le_n_ticket->text().trimmed() : displayInvoiceId()`. | S |
| F5 | Medium | bug | imprimir.cpp:550, sql_lite.cpp:873-891 and :894-910 | `paidMarker = ticketAllGarmentsPaid(db, ticket)` counts voided (Anulado) rows, whose `pagado` stays "NO" (voidGarmentRow comment), so a ticket with one voided garment and everything else paid never prints IMPORTE PAGADO on a reprint even though every printed garment is paid. | Exclude `estado='Anulado'` in `ticketAllGarmentsPaid` (and test it) or compute the marker from the loaded model. | S |
| F6 | Medium | design | imprimir.cpp:244-273 | QR reprint recomputes the URL from `SUM(importe)/(1+iva)` with the CURRENT `ivaRate` and re-asks the backend, with a blocking nested `QEventLoop` (5 s). The direct backend only encodes locally (`generateQRAsync` -> `answerLater`, aeatdirectbackend.cpp:502-510), so the wait is for nothing there, and the amount can diverge from the submitted one (rows edited after submit, IVA rate changed). The invoice already stores `verifactu_url_qr` (selfcheck.cpp:127 already relies on it). The nested loop re-enters the UI: the button and Enter stay enabled, the dialog can be closed (lambda captures stack refs; `this` is the connection context) and in "Todas las facturas" the worst case is 5 s per invoice. | Draw the reprint QR from the stored `verifactu_url_qr` (`AeatQr::image`) when the paid rows of the seq agree on it; keep the GetQrCode path only for the gateway, and then disable the dialog (`setEnabled(false)`/wait cursor) during the wait; guard with a `QPointer`. | M |
| F7 | Medium | bug | imprimir.cpp:516-529 | "Todas las facturas" loop never checks that an event loaded rows: an event whose paid rows were all voided/empty prints an empty ticket (same as F1), and a failure of one `printTicket` is not distinguished in the panel (always "Facturas impresas: ..." in green, then red problems). | Skip events with `rowCount()==0` and list them as not printed; show a warning icon when `m_printProblems` is not empty instead of `okHtml`. | S |
| F8 | Low | compliance | imprimir.cpp:348-352 | Leyenda decision looks only at row 0 of the loaded set. In an unscoped reprint row 0 can be an unpaid/other-event row, giving no leyenda next to a valid QR, or the opposite. | Decide from the paid rows' aggregate (all ENVIADA) or from the seq-scoped set; add a test. | S |
| F9 | Low | bug | imprimir.cpp:154-159 | `checkTicketPaid` treats anything but "NO" as paid, while `getTicketInfo` (scoped) and `ticketAllGarmentsPaid` use strict `pagado='SI'`. Blank/NULL pagado rows print on an unscoped factura. | Use `== "SI"` (documented in the same file at :550). | S |
| F10 | Low | design | mainwindow.cpp:1063-1090, 1366-1396; pay_dialog.cpp:416; recog_prendas.cpp:928; selfcheckdialog.cpp:229 | Every engine/dialog `new Imprimir(db, this)` is parented to a long-lived window and never deleted (also each `getTicketInfo` allocates a new `QSqlQueryModel(this)` without freeing the previous one, imprimir.cpp:111). Leak per ticket saved/printed during a shift. The menu dialogs have no `WA_DeleteOnClose`. | `setAttribute(Qt::WA_DeleteOnClose)` for the menu dialogs, stack object or `deleteLater()` for engine uses; `delete sqlQueryModel` (or reuse one model) in `getTicketInfo`. | S |
| F11 | Low | design | imprimir.cpp:111-138 | `getTicketInfo` calls `db.open()`/`db.close()` on the shared connection; `QSqlQueryModel` fetched lazily (rows cached only up to the first block). It closes a connection a caller may have opened, and the model is read after close. Same pattern in the rest of the codebase, so only a note. | Eager `while (canFetchMore) fetchMore()` before close, or copy rows into a struct list. | S |
| F12 | Low | bug | imprimir.cpp:540-545, 565 | A factura printed on a never-submitted ticket (Verifactu off, `events` empty) prints all paid rows of all payment events combined with the bare n_recibo, no QR; panel says "Sin código QR: ... no está confirmada por AEAT" which misleads when Verifactu is simply disabled. | Distinguish "Verifactu desactivado" from "pendiente" in the message. | S |
| F13 | Low | dead-code | imprimir.cpp:317-320 | `size != ""` can never be false (`QString::number(...,'f',2)` always yields text). Only the "0.00" test matters. `ui_impr->setWindowTitle(...)` in mainwindow.cpp:1371-1393 is also overridden by nothing (UiKit::setUpDialog sets "Imprimir" at :30) - harmless. | Drop the dead test. | S |
| F14 | Low | design | imprimir.cpp:437-439, 424-425 | Printer errors on the engine path pop up modal `QMessageBox` while `Imprimir` is hidden (`isVisible()` false) - intended and documented; but `printFra`/`printRecibo` print the second copy even if the first failed, producing two message boxes in a row. | Stop after the first failure. | S |
| F15 | Low | docs | see next section | Drift between docs and code. | - | S |

Verified OK (no finding): decimal comma (`displayMoney`/`displayDecimal` at :316-323, no raw `QString::number` of money on paper); `INGRESOS_COL_*` indices used throughout; prepared statements everywhere; QR only on facturas (:337); QR not reused between prints (:451, 524, e2e covered); Enter prints once (UiKit::onEnter); `(estado IS NULL OR estado != 'Anulado')` handles NULL correctly (:114-118); partial seq scoped with `pagado='SI'` (the seq-0 trap at :120-123 is real, see sql_lite.cpp:1823).

## Documentation drift

| Doc (file:line) | Says | Code (file:line) |
|-----------------|------|------------------|
| docs/modules/imprimir.md:123-134 | QR emitted "only when at least one row has a non-empty verifactu_csv" | imprimir.cpp:233-242: without CSV the direct client's queued record QR (`qrBeforeReply`) is used and sets `m_qrFromQueuedRecord`. Not documented anywhere in the module doc. |
| docs/modules/imprimir.md:136-142 and docs/INDEX.md:176 | Raster is `qr.scaled(192,192).toImage()`; leyenda only when row 0 estado == ENVIADA | imprimir.cpp:339-343 uses `TicketRenderer::printableQr` (30-40 mm, whole dots, quiet zone) and falls back to the raw image; :350-352 also prints the leyenda for PENDIENTE when `verifactuIssued` or the QR came from the queued record. |
| docs/modules/imprimir.md:119-121 | "`resolveQrCode()` is unchanged by the ESC/POS migration" | stale: the function now has the direct-client branch and the `qrCode` cache (:172-173, 240). |
| docs/modules/imprimir.md:132-134, INDEX.md:176 | QR fetched via REST `/GetQrCode` | with the direct backend this is a local encode (aeatdirectbackend.cpp:502-510); doc should say which backend and that `verifactu_url_qr` is not used. |
| docs/modules/imprimir.md:25-35 (Key interface) | omits `invoiceSeq`, `verifactuIssued`, `ticketBytes()` | imprimir.h:43-49, 76 |
| docs/modules/imprimir.md:50-56 | `AND estado != 'Anulado'` | imprimir.cpp:126, 132: `(estado IS NULL OR estado != :anulado)` and the seq branch also forces `pagado='SI'` (:124) - the doc says "optionally scoped to a paid seq" but not the trap. |
| docs/modules/imprimir.md:62-70 | `StatusApiPrinter::sendAndReadStatus()` | imprimir.cpp:389 calls `sendAndReadStatusBounded(..., 10000)` (worker with 10 s watchdog), plus fatal-status early return :401. |
| docs/modules/imprimir.md:15 (dialog) | factura enumerates partial payments | The events query (imprimir.cpp:477-493), the "IMPORTE PAGADO only if all paid" rule (:546-551), the recibo-fallback in PayDialog (pay_dialog.cpp:418-427) and the callers' flags are not described. |
| docs/modules/imprimir.md:144-153 (Dependencies) | lists `generateQRAsync` | also `qrBeforeReply`, `uikit`, `AppSettings::useStatusApi`; CMake links uikit. |
| docs/architecture.md:59 | save-time print uses `verifactuIntegration = nullptr` "so no QR fetch is attempted" | accurate for printFra but the QR comes from the preset `qrCode`; clarify. |
| docs/testing/unit_suites.md | no entry for imprimir | the module has no unit suite; its behaviours are only listed in e2e.md:93-96. |

## Test gaps

Current coverage (all e2e): `test_reprintPaymentEvent_scopedRowsAndQrGating` (scoped rows, GetQrCode InvoiceID/date, ANULADA no QR), `test_imprimirDialog_reciboFacturaCompletaAndPartialPayments` (unknown number, recibo, completa DNI/address, two events and chosen one), `test_imprimirDialog_qrNotReusedForTheNextTicket`, `test_enterInNumberFieldRunsOneAction`, direct-backend leyenda/QR test (test_e2e_app.cpp:1789-1817), plus `test_ticket_preview`/`test_escpos` for the renderer and UI smoke C1-C3. Not covered: everything below.

### Unit (proposed new `test_imprimir`, offscreen, temp SQLite DB, `ticketBytes()` for the rendered text; no AEAT)
- `getTicketInfo_excludesAnuladoRows`: rows estado Anulado and NULL estado -> NULL kept, Anulado dropped, unscoped and scoped.
- `getTicketInfo_seqScopeKeepsOnlyPaidRowsOfThatSeq`: seq 0 with unpaid siblings (the :120-123 trap).
- `getTicketInfo_trimsTicketNumber` (F1): " 650 " loads the rows.
- `buildTicket_emptyModelPrintsNothing` (F1): all garments Anulado -> `ticketBytes()` empty, `printTicket()` false.
- `recibo_numberIsTicketNumberNotInvoiceId` (F4): rows with ids "651-1" first.
- `factura_listsOnlyPaidRowsAndTotals`: mixed paid/unpaid, unscoped; total = paid sum; base+IVA = total (cent rounding, e.g. 10.01, 0.05).
- `factura_unscopedMultiSeqNoQr` (F2): two events, `invoiceSeq=-1` -> no QR; `resolveQrCode` amount ignores unpaid rows (needs fake `VerifactuIntegration`/direct backend with temp store; reuse e2e seam or assert the requested total).
- `resolveQrCode_blockedByAnuladaRectificadaError`: each of the three estados -> null, no request.
- `resolveQrCode_noPaymentDateReturnsNull`: invalid/empty fecha_pago.
- `resolveQrCode_usesFirstValidFechaPagoNotRow0`.
- `resolveQrCode_cachedQrCodeBypassesChecks_andIsResetByDialogPrint` (documents preset behaviour).
- `leyenda_onlyForEnviada_orIssuedPending`: ENVIADA yes; PENDIENTE no; PENDIENTE + `verifactuIssued` yes; ERROR/ANULADA no (renderer text check "Factura verificable").
- `leyenda_row0IsUnpaid` (F8).
- `recibo_neverHasQr` with preset `qrCode` and isRecibo.
- `paidMarker_onlyWhenAllGarmentsPaid_ignoringVoided` (F5) + `ticketAllGarmentsPaid_ignoresAnulado` in test_sql_lite.
- `completeInvoice_emptyAddressUsesClientAddress`, `completeInvoice_blankDniStillPrints`.
- `size_displayedWithCommaAndHiddenWhenZero`: "1.50" -> "1,50"; "0" hidden.
- `paperWidth58Maps420`: ticket rendered at 58 mm width (check line lengths).
- `printTicket_noBuild_returnsFalse`; `sendIfEnabled_printingOff_returnsFalse`; `printTicket_failureReportsInPanelWhenVisible_elseMessageBox` (needs `ThermalPrinter`/status seam: inject a fake sender, S-M).
- `displayInvoiceId_fallbacksToTicketNumber` (the pure seam is already tested in test_sql_lite; this is the gather).
- `factura_cancelledEventRefused` (F3) once decided.

### E2E (test_e2e_app)
- `test_imprimirDialog_trailingSpaceAndFullyVoidedTicket` (F1): result panel message, no bytes printed, no popup.
- `test_imprimirDialog_allInvoicesPrintsEachOnce`: "Todas las facturas" -> N jobs/QR requests with distinct InvoiceIDs; one failing event keeps the others.
- `test_recogidaReprint_noRowClicked_multiEvent` (F2): Recogida `printFactura(-1)` on a 2-event ticket must not print a combined QR.
- `test_reprintCancelledInvoiceRefused` (F3) / `test_reprintRectifiedInvoice`.
- `test_imprimirDialog_gatewayTimeoutPrintsWithoutQr` (fake server delays GetQrCode > 5 s; panel says "AEAT no respondió a tiempo"; second print works; no re-entrancy: `click()` twice while waiting prints once).
- `test_imprimirDialog_printerFailureShownInPanel` (fake ThermalPrinter failing): panel red, not "impreso" green only.
- `test_imprimirReciboPartiallyPaid_noImportePagado` and `..._voidedGarment_importePagado` (F5).
- `test_imprimirFactura_verifactuDisabled_noQrMessage` (F12).
- `test_payDialogPartialFactura_scopedAndReciboFallback`: PayDialog fallback recibo shows IMPORTE PAGADO when AEAT did not reply (pay_dialog.cpp:418-427) - currently only comments.

### UI smoke (tests/ui/block_c.py)
- C3b: ticket with two partial payments (Cobrar part, then rest) -> Imprimir -> Factura shows the "2 facturas" list; print one and "Todas" and decode both QRs (distinct numserie) with `qr_text`.
- C3c: Imprimir -> Recibo of a ticket with a voided garment: voided line absent, IMPORTE PAGADO present (F5).
- C3d: Imprimir -> Recibo with trailing space / unknown number: panel message, zero print jobs (`new_jobs(run, before, 0)`).
- C3e: Recogida "Imprimir factura" with a row clicked vs not clicked on the same 2-event ticket.
- C2 additionally: decode the QR total (`importe=`) and check it equals the printed TOTAL.
- 58 mm paper setting job (once): no clipping, QR fits (<=420 dots).

### Self-check (SelfCheckDialog M3 / selfcheck.cpp)
- `printSample` passes a preset QR from `verifactu_url_qr`, so the real reprint path (`resolveQrCode`, events, `ivaRate` recomputation) is never exercised on the shop's data. Add a check M2b that, for the chosen invoice, builds the factura through `resolveQrCode()` (without printing) and verifies the QR URL equals the stored `verifactu_url_qr` (catches IVA-rate/amount drift on real data, F6). M, test in `test_selfcheck`.
- Also verify the chosen invoice has no unpaid/voided sibling of other seq that would mix (F2): the SQL at selfcheck.cpp:124-135 already guards estados; add an assertion for seq uniqueness of printed rows.

## Questions / cross-module notes
- printing reviewer: `TicketRenderer::render` is given an empty `TicketData` when the model is empty (F1); should the renderer refuse (`garments.isEmpty()`)? Also confirm `Nº:` label for recibo/factura (ticketrenderer.cpp:31) and that base/IVA rounding rule matches `AeatHash::amountText` of the QR total (ticketrenderer.cpp:55 vs aeat QR).
- verifactu-direct reviewer: should the reprint use `verifactu_url_qr` / the stored record (F6)? Is `verifactu_csv` ever set by the direct backend (it decides the `hasCsv` branch at imprimir.cpp:233)? Is `queuedRecordQr` still valid after the record is ENVIADA (it requires state kPending) - that is why `hasCsv` is needed.
- recogida reviewer: `RecogPrendas::on_pb_print_clicked` with no clicked row (F2); `printFactura` ignores a failed `getTicketInfo` empty model.
- Supervisor: compliance wording for printing a cancelled invoice (F3) needs a legal decision (print with ANULADA legend vs. refuse).
- The `isRecibo = qrCode.isNull()` fallback in `PayDialog::printPartialFactura` (pay_dialog.cpp:417) prints a RECIBO document in place of the invoice when AEAT did not reply; confirm that is the intended legal behaviour for a paid ticket (the invoice is later reprintable via Recogida).
