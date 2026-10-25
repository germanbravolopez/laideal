# Review: add_garment (src/add_garment/) - before release 12.0

## Summary
- Health: good core (paid-ticket gate at search and again at save, retyped-number guard, closed-quarter check on the payment date, invoicing-blocked seam, result panel, decimal comma, shared insert seam). No Critical findings.
- Main risks: free-typed garment names and the price lookup (modal QMessageBox pop-ups, 0 EUR / negative-size garments that can be invoiced to AEAT), the hard-coded seq 0 for the paid case, and no unit/e2e test for most validation branches.
- Docs: add_garment.md is mostly accurate; price section and validation list lag the code (garmentImporte, invoicingBlocked); one stale unit test (`test_insertGarmentRow_addGarmentShapePending`) contradicts the current SIN COBRAR rule.
- Coverage: only 2 e2e tests (+2 incidental) and 2 sql_lite tests touch it; no dedicated suite; self-check and UI smoke exercise it very little.

## Findings

| Sev | Category | file:line | Finding | Proposal | Effort |
|---|---|---|---|---|---|
| High ✅ FIXED 19-10-2026 | bug | add_garment.cpp:134-140, 230-242; sql_lite.cpp:501-545 | `setGarmentPrice` calls `readGarmentPrice` whenever Cantidad / Tamaño / Servicio change, even with an empty or free-typed Prenda (the combo is editable, line 68; the exact-match guard at 137-140 covers only the Prenda-changed path). `readGarmentPrice` then shows a modal `QMessageBox::warning` ("No se ha encontrado la prenda") - e.g. typing the quantity before choosing the garment. Breaks the "no pop-ups, everything in the result panel" rule; UI smoke B4 and e2e always pick the garment first so it is never hit. Also `LIKE :garment` means `%`/`_` typed in the combo match other garments. | In `setGarmentPrice`, return importe 0 (and no lookup) unless `m_cbPrenda->findText(text, Qt::MatchExactly) != -1`; optionally make the combo non-editable-but-searchable (`QCompleter`) or refuse unknown names in `validationError`. | S |
| High ✅ FIXED 19-10-2026 | bug / compliance | add_garment.cpp:257-261 (validationError) | No check that the garment exists in `prendas` nor that importe > 0. A free-typed name (or "0" quantity - `QIntValidator(1,9999)` at :76 treats "0" as Intermediate, so it still reaches save) saves a 0,00 EUR row; if "Pagada" is ticked it is submitted to AEAT as a 0 EUR invoice at seq 0 (and the ticket is then locked for further additions). MainWindow refuses "ninguna prenda con importe" (mainwindow.cpp:~823). | Refuse when `m_cbPrenda` text is not an exact `prendas` entry, when quantity <= 0, and when the importe is 0 and Pagada is ticked (or always, to match saveTicket). | S |
| Medium ✅ FIXED 19-10-2026 | bug | sql_lite.cpp:1453-1462 (used at add_garment.cpp:238); add_garment.cpp:78-81 | `leSize` has no validator and `garmentImporte` clamps only before multiplying by size: `garmentImporte("3","-2",4.0)` = -24. A negative size on a non-m2 garment (or any garment; `garmentUnmeasured` only checks m2 names, sql_lite.cpp:1464) stores a negative importe, which when paid becomes a negative simplified invoice. Same size factor is applied to garments not named "m2" whenever a size is typed (documented). Non-numeric text ("abc") is silently 0. | Clamp after the size multiplication in `garmentImporte` (shared fix, also protects Recogida), and give `leSize` a `QRegularExpressionValidator` for non-negative decimals with dot/comma; refuse a non-empty unparsable size. | S |
| Medium | compliance | add_garment.cpp:330-332, 318-321; mainwindow.cpp:1431-1434; recog_prendas.cpp:730-731; pay_dialog.cpp:241-243 | Paid garment always assumes seq 0 / InvoiceID = n_recibo ("always the ticket's first payment event"). PayDialog instead uses `nextVerifactuInvoiceSeq`, which also skips numbers for which the direct client already generated an `aeat_records` alta (sql_lite.cpp:1892-1909), e.g. a payment whose garments could not be stored as paid. In that case AddGarment would submit a second invoice under an already used InvoiceID (AEAT duplicate / chain problem), and the row's `verifactu_invoice_seq` would be 0 too. | Compute the seq with `nextVerifactuInvoiceSeq(db, ticket, env)` at save, store it on the row and pass it in `paidGarmentSaved` (add a `seq` argument) so both receivers use it; or refuse the add when `aeat_records` already has an alta for the ticket. Ask verifactu-compliance-auditor. | M |
| Medium | compliance / design | add_garment.cpp:212-213, 268; mainwindow.cpp:831 | The closed-quarter check covers only the payment date. A new garment copies the ticket's `fecha_recepcion` (line 304); if that date lies in a closed quarter the app adds a row to a closed quarter (saveTicket refuses exactly this, mainwindow.cpp:831; contabilidad groups by `fecha_recepcion`, contabilidad.cpp:109). | Refuse (or warn) when `quarterIsClosed(db, m_deFechaRecepcion->date())`, paid or not; add test. Confirm the rule with the owner. | S |
| Medium ✅ FIXED 19-10-2026 | bug | add_garment.cpp:97, 262-270 | Payment date is unrestricted: a future date (QDateEdit has no maximum) or a date before the reception date is accepted and sent to AEAT as `InvoiceDate`. No other path validates this either (PayDialog has no min/max), so it is a cross-module question. | Max = today, min = reception date for both date editors in AddGarment; same for PayDialog (supervisor). | S |
| Medium | unit-tests | tests/ (none) | No unit/offscreen suite for AddGarment: validation order, closed quarter, m2, incomplete form, comma size, row shape written, signals emitted are untested at any layer except 2 e2e paths. | Add `tests/test_add_garment.cpp` (see Test gaps). | M |
| Medium | unit-tests | tests/test_sql_lite.cpp:1725-1748 | `test_insertGarmentRow_addGarmentShapePending` is stale: its comment says AddGarment books PENDIENTE (issue #41) but the row is `pagado=NO`/`PENDIENTE`, which is exactly the shape the code no longer produces (unpaid = `SIN COBRAR`, add_garment.cpp:319-321). It documents a wrong rule. | Change to `pagado="SI"` + `PENDIENTE` and add the `NO` + `SIN COBRAR` case, or fold into the new AddGarment suite asserting the stored estado. | S |
| Low | design | add_garment.cpp:290-295 | After every save `resetAllContents()` clears the receipt, so adding a second garment to the same unpaid ticket requires re-typing the number. For paid saves this is required (ticket now refused), for unpaid it costs time. | Keep the searched ticket (and re-run the search) after an unpaid save. | S |
| Low | design | add_garment.cpp:192-200, 27, 211-213 | `onSearchClicked` loads `SELECT *` into a `QSqlQueryModel` just to read two columns and runs the query for an empty ticket; db opened/closed around the model (relies on the model keeping its cache). The empty check could precede the query. | Select only `cliente, fecha_recepcion ... LIMIT 1`; check `ticket.isEmpty()` first. | S |
| Low | design | add_garment.cpp:192-207, 253 vs 300-327 | Paid check and insert are not in one transaction (check-then-insert). Single-user app, so only theoretical. | Optional: do the check inside `insertGarmentRow` variant. | M |
| Low | design | add_garment.h:31 | `ticketFound` is a public data member used by tests only; `invoicingBlocked` unset means "paid refused" (good, fail-safe). | Make `ticketFound` a const accessor; keep the fail-safe default. | S |
| Low ✅ FIXED 23-10-2026 | bug | add_garment.cpp:309 | `estado = "Recogido"` with a pickup date can be saved with the pickup date before the reception date (no check), and for an unpaid garment on a not-yet-collected ticket. Probably intentional (pay at pickup). | Min date = reception date on `deFechaRecogida`. | S |
| Low | guidelines | add_garment.cpp:25-26 | Comment density and English identifiers are fine; Spanish UI strings are not wrapped in `tr()` (1 `tr` use at :264 vs plain literals elsewhere) while Recogida uses `tr`. Inconsistent inside the file. | Pick one convention (project default per coding-guidelines) and apply. | S |
| Info | dead-code | add_garment.* | No dead methods found: `loadTicket`, both signals, `invoicingBlocked`, `validationError` are all used (MainWindow, Recogida, e2e). `QDebug` log at :248, :254 are warnings, ok. | - | - |

Verified OK (no finding): prepared statements only (:194, sql_lite.cpp:940); HTML-escaped ticket in all result messages; paid refusal repeated at save (:253); retyped number guard (:247); invoicing blocked default is refuse (:263); comma size stored with dot (:312) and importe normalised by `moneyText` in the insert; displayMoney in the panel/importe; amount sent to AEAT is the garment's importe (`moneyText(...).toDouble()`) and the date is the payment date; estado SIN COBRAR/PENDIENTE matches saveTicket; `edit_lock` '0' set by the seam.

## Documentation drift

| Doc | Says | Code |
|---|---|---|
| docs/modules/add_garment.md "Price calculation" (formula block + paragraph) | `price = quantity * readGarmentPrice(...)`; `price *= size`; AddGarment normalises with `.trimmed().replace(",", ".")` and `toFloat()` | add_garment.cpp:236-241 delegates to `garmentImporte` (sql_lite.cpp:1453): doubles, price re-rounded to cents, result rounded to cents, clamped >= 0 before size. The doc describes the pre-refactor code. |
| add_garment.md "Validation" | lists 4 rules | add_garment.cpp:262-267 also refuses when `invoicingBlocked` is non-empty (or unset: "Verifactu no está disponible") and :257 "Formulario incompleto" requires a client too; order of checks is not documented. |
| add_garment.md "Key interface" snippet | `new AddGarment(db, this)` only | Callers must set `invoicingBlocked` (mainwindow.cpp:1426, recog_prendas.cpp:723); unset = every paid add refused. Missing from the snippet (the field is in add_garment.h:29-31). |
| add_garment.md Workflow 4 "always seq 0" | always the first payment event | See Medium finding on `aeat_records`-used numbers (sql_lite.cpp:1892-1909). |
| docs/architecture.md:114-115 | "Adds new garment rows to an existing ticket" | Omits: refuses tickets with a paid garment, paid add = AEAT invoice. |
| docs/testing/unit_suites.md:43 | says insertGarmentRow covers "`saveTicket` and AddGarment shapes" | The AddGarment-shape test is stale (see test finding). `ticketHasPaidGarment` is listed at :42, fine. |
| docs/testing/ui_smoke.md:65, tests/ui/block_b.py:66-108 | B4 adds unpaid Falda, B6 refusal | consistent; nothing covers paid add or Tickets-menu path. |
| docs/modules/sql_lite.md:70 | `insertGarmentRow`: "AddGarment (fresh row...)" | The seam is also used by tests only for single rows; saveTicket uses `insertGarmentRows` (mainwindow.cpp:1047). Minor wording. |
| docs/testing/e2e.md:114-115 | rows for the two tests | match tests (test_e2e_app.cpp:2630, 2665). `test_recogida_addGarmentFromTheWindow` (:1359) and the blocked-state tests (:1009, :1041, :1115) are documented only partially for AddGarment. |

## Test gaps

Existing coverage map:
- sql_lite: `test_ticketHasPaidGarment` (test_sql_lite.cpp:1752), `test_insertGarmentRow_*` (:1665-1746, last one stale), `test_garmentImporte` (:624), `test_garmentUnmeasured` (:646).
- e2e: `test_addGarmentPaid_submittedAtOnce` (:2630: first invoice seq 0, amount, date, panel, no pop-up, sibling SIN COBRAR), `test_addGarment_retypedTicketNumberRefused` (:2665, also paid ticket search refusal), `test_recogida_addGarmentFromTheWindow` (:1359), blocked chain (:1009 Tickets menu invoicingBlocked; :1041 Recogida refusal; :1115 not configured and unset check), Enter-key repeated search (:2006).
- UI smoke: B4 (unpaid Falda from Recogida), B6 (paid ticket refused).

Proposed unit (new `tests/test_add_garment.cpp`, offscreen, QTemporaryDir DB with `prendas`, one unpaid ticket, one paid ticket, closed quarter via the existing seam):
- `test_search_unknownTicketWarns_formStaysDisabled`
- `test_search_emptyNumber_notFound`
- `test_search_paidTicketRefused_noFormEnabled`
- `test_save_withoutSearch_refused`; `test_save_retypedNumber_refused` (no row written)
- `test_save_ticketPaidMeanwhile_refused` (search unpaid, mark a row SI, save -> "ya tiene prendas pagadas")
- `test_save_incompleteForm_refused` (no prenda / no cantidad)
- `test_save_m2GarmentWithoutSize_refused` and `test_save_m2GarmentWithCommaSize_importeAndStoredDot`
- `test_save_paidWithQuarterClosed_refused_unpaidAllowed`
- `test_save_paidInClosedReceptionQuarter` (documents the decision on the Medium finding)
- `test_save_paidWithoutInvoicingCheck_refused` / `_blockedReason_shown` / `_emptyReason_saves`
- `test_save_unpaid_rowShape` (pagado NO, SIN COBRAR, fecha_pago empty, estado En tienda, edit_lock 0, 16-char hash, signal `garmentSaved` once, `paidGarmentSaved` zero)
- `test_save_paid_rowShape_andSignal` (PENDIENTE, fecha_pago = date, signal args ticket/date/amount to 2 decimals)
- `test_save_pickedUp_estadoRecogidoAndDate`
- `test_save_observationsStored`
- `test_price_updatesWithQuantityServiceAndSize` (Limp. vs Plan., "2,6" m2)
- `test_price_emptyOrUnknownGarment_noPopup` (use the project popup catcher; fails today -> High finding)
- `test_quantity_zeroRefused`, `test_size_negativeRefused` (fail today)
- `test_zeroImporte_paidRefused` (fails today)
- `test_afterSave_formClearedAndWindowOpen`
- `test_reset_button_clearsAndDisablesGroups`
- sql_lite: `test_garmentImporte_negativeSize_clampsToZero` (fails today: expect 0).

Proposed e2e (tests/test_e2e_app.cpp):
- `test_addGarment_unpaid_savedSinCobrar_noAeatCall` (Tickets menu, no Create request, panel text, Recogida refresh).
- `test_addGarment_closedQuarterRefused` (paid in a closed quarter: nothing inserted, no Create).
- `test_addGarment_aeatRejects_rowRefusedAndRetryable` (fake AEAT rejection after a paid add: estado ERROR/refused list counts it, Recogida Reintentar works).
- `test_addGarment_noAeatAnswer_staysPendienteForRecovery` (documented behaviour at add_garment.md Workflow 4).
- `test_addGarment_afterPartialPayInRecogida_refused` (ticket paid via PayDialog -> search refused; closes the seq assumption).
- `test_addGarment_numberAlreadyUsedAtAeat_usesNextSeq` (once the seq finding is fixed).
- `test_addGarment_noPopups_typingQuantityFirst` (guards the High finding).
- `test_addGarment_closeWithoutSaving_recogidaReportsNone` (Recogida "No se ha añadido ninguna prenda").

UI smoke (tests/ui/block_b.py, ui_smoke.md):
- B6c: Tickets menu path, add a **paid** Falda to an unpaid ticket (fake AEAT accepts it: `ENVIADA`, CSV, seq 0), then search the same ticket again and see the refusal. Today only the unpaid Recogida path runs on real widgets.
- B6d: m2 garment without size shows the refusal in the panel; with "2,5" the Importe shows the decimal comma.
- Check the Alt+key / Enter-to-search real keystroke path (UiKit::onEnter) and the disabled groups before searching.

Self-check (`laideal.exe --self-check`, src/app/selfcheck.cpp, test_selfcheck.cpp): AddGarment is not touched. Propose a read-only check "tickets with garments added after payment": any ticket where a row with `pagado='NO'` or a later-created row exists beside a seq-0 sent invoice whose total does not cover the paid rows (guards the paid-gate invariant on the user's own data), and "AddGarment-created paid rows have `verifactu_estado` PENDIENTE/ENVIADA, never blank". Effort M, ask the self-check reviewer.

## Questions or cross-module notes
- Supervisor / sql_lite reviewer: `readGarmentPrice` (sql_lite.cpp:501) shows QMessageBox pop-ups from a data-layer function; any UI caller can trigger them. Fix at the source (return a status) vs guard in the callers.
- sql_lite / MainWindow reviewer: `garmentImporte` negative size clamp (shared with MainWindow `setGarmentPrice` and Recogida m2 edit).
- Verifactu auditor: seq 0 assumption vs `nextVerifactuInvoiceSeq` + `aeat_records` (also affects `retryVerifactuSubmit(ticket, 0)` from Recogida, recog_prendas.cpp:731).
- PayDialog / Recogida reviewers: future or pre-reception payment dates (pay_dialog.cpp:232 area) and closed-quarter rule for the reception date.
- Question for owner: should a new garment on a ticket whose reception date is in a closed quarter be allowed (contabilidad groups by `fecha_recepcion`)?
