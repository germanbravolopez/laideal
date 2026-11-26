# Verifactu / SIF — Legal Requirements vs. La Ideal Coverage

La Ideal works in **VERI\*FACTU** mode: each invoice record is sent to AEAT at once. (The alternative, "NO VERIFACTU", also needs a signature on every record and a richer event log.) How the integration works is in [README.md](./README.md); this file lists, per requirement, the rule and where the code meets it.

Sources:
- [Real Decreto 1007/2023 (BOE-A-2023-24840)](https://www.boe.es/buscar/act.php?id=BOE-A-2023-24840): the regulation.
- [Orden HAC/1177/2024 (BOE-A-2024-22138)](https://www.boe.es/buscar/act.php?id=BOE-A-2024-22138): technical specifications.
- [Ley 11/2021, Ley Antifraude (BOE-A-2021-11473)](https://www.boe.es/buscar/act.php?id=BOE-A-2021-11473): art. 29.2.j) LGT, the parent law.
- [AEAT: Sistemas Informáticos de Facturación y VERI\*FACTU](https://sede.agenciatributaria.gob.es/Sede/iva/sistemas-informaticos-facturacion-verifactu.html).

Status: **COVERED** = satisfied; **PARTIAL** = partly, or relies on a third-party component; **MISSING** = not satisfied (a release-gating issue is then filed in `docs/progress_tracker.md`, naming the requirement number).

---

## 1. Integridad e inalterabilidad de los registros

> Art. 8.1 RD 1007/2023: los registros "no puedan ser alterados sin que el sistema informático lo detecte y avise de ello". An error is corrected by **anulación** or **factura rectificativa**, never by deleting.

**COVERED.** Tests: `test_sql_lite.cpp`, `test_aeat_chaincheck.cpp`, `test_e2e_verifactu.cpp`, `test_e2e_app.cpp`.

No UI path edits or deletes an invoice:
- `Listado::on_actionEliminar_fila_triggered()` refuses deleting from `ingresos`; `Listado::populateTable()` sets `NoEditTriggers` on that view. Changes go through RecogPrendas / PayDialog / CancelInvoiceDialog / RectifyInvoiceDialog, which keep AEAT and the accounting lock in step.
- Database guards (`migrateDatabase`): trigger `ingresos_registered_no_delete` refuses deleting a row registered at AEAT (a CSV, or `ENVIADA` / `ANULADA` / `RECTIFICADA`); trigger `ingresos_accepted_identity` refuses changing the identity of an `ENVIADA` row, or of a paid `ANULADA` / `RECTIFICADA` one (still exported): `n_recibo`, `fecha_pago`, `pagado`, CSV, XML, `verifactu_hash`, InvoiceID, seq. Estado, cancellation date, pickup and amount still move (Separar prendas keeps the invoice total over the two parts).
- `migrateDatabase`'s one-time repairs touch only rows never part of a registered invoice, or relink one to the invoice AEAT holds; they never change the invoice data of a `pagado = 'SI'` row and log what they change (README §`VerifactuEstado`).
- Amount writers (size, quantity, service, manual price) refuse a paid / sent row themselves, besides the read-only Recogida fields. Amounts are stored in cents, the value charged and sent (`moneyText`); an unreadable amount is refused. A negative hand-typed price is refused.
- An m2 garment without its size (`garmentUnmeasured`) is never invoiced (saving as paid and Cobrar refuse it): the invoice would reach AEAT at 0 with its amounts frozen. A paid ticket sends exactly the sum of the rows it stored.
- Separar prendas on a paid row copies the original's `verifactu_*` columns to the new row, giving the split-off part its `importe` in cents and the original the exact remainder (`sql_lite::splitGarmentRow`; refused on a row locked by Contabilidad).

Writing AEAT's answer back:
- Insert-and-submit paths: ticket save, Cobrar, a paid garment in Añadir prendas (only on a ticket with no paid garment, re-checked at save) and the rectificativa. A reply is written by `updateTicketVerifactuFields`, which never rewrites an `ENVIADA` / `ANULADA` / `RECTIFICADA` row, so a late duplicate rejection cannot wipe a registered invoice's CSV, payload or huella. `markTicketRectified` never marks a row already `ANULADA` / `RECTIFICADA`.
- A duplicate answer is adopted only when AEAT's own record (queried) has our amount and huella and an earlier send reached AEAT; another amount is an `ERROR` (README §Sending and outcomes).

Cancellation (`CancelInvoiceDialog` -> `cancelInvoiceAsync` -> AEAT):
- One request per payment event, under the date the invoice was registered with (its earliest `fecha_pago`, `submittedInvoiceEvents`); then `sql_lite::markInvoiceSeqCancelled` sets `ANULADA` on the event's paid rows and records `fecha_anulacion`: the payment period keeps its income, a filed Contabilidad report is never altered, and the cancellation is accounted where it happens. Rows already `ANULADA` keep state and date; an empty cancellation XML is filled by a later call; the closed-quarter check is inside the statement.
- Two presses (the first says what will be cancelled). Refused while an accepted rectificativa points at the ticket (its delta would stay in the books and AEAT would hold a rectificativa of a cancelled invoice).
- A lost reply is resent as the same record; AEAT's "already cancelled" settles it as cancelled, so books and AEAT agree and Anular does not loop.
- An invoice the real AEAT never held (before production) is cancelled in the books only, with a log warning and an `<AnulacionLocal>` note the export shows as `local="1"` (`CancelInvoiceDialog::cancelLocally`; README §Going to production).

Rectification (`RectifyInvoiceDialog` -> `submitRectificationAsync` -> AEAT):
- R1-R5 and both `RectificationType` variants (`S` sustitución, `I` diferencias). A substitution marks the original's paid rows `RECTIFICADA` (`markTicketRectified`) with `fecha_anulacion` = the rectificativa's date: the original keeps its payment period, the rectificativa's period subtracts it. The new row links back by `verifactu_rectifies_n_recibo`.
- The rectificativa may not be dated in a closed quarter nor before the original payment. A refused one counted in a quarter closed since is resent only as filed (date, amount, S/I mode), its row untouched but `PENDIENTE` while sent; the original is then marked in an open period (`openPeriodDate`: that date, else today, else the next open quarter).
- Gateway limit: its rectificativas carry no `RectificationItems` (`FacturasRectificadas`, optional in AEAT's schema; the direct client sends it), so the facade refuses one with the gateway in PRODUCTION ("use la conexión directa").

Detecting tampering (the regulation requires the *software* to detect, not the OS to prevent; SQLite can be edited with file access). With the direct client `AeatChainCheck::verify` recomputes the huella of every stored `aeat_records` record from its XML and checks each link to the previous record, per issuer. It runs at every start (`AeatDirectBackend::verifyStoredChain`: red result-panel message and `qCritical` on failure) and in the post-update self-check.

| Detected | How |
|---|---|
| An altered hashed field (number, date, type, amounts, links) | Recomputed huella differs |
| A reordering, a second `PrimerRegistro` | Link check |
| A deleted record | The next one links to a huella this database lacks; accepted only when it is an anchor (`aeat_chain_anchors`: heads a chain sync adopted from AEAT, as a restored copy does). A store from before the anchors takes that day's continuations as its first anchors, once |
| Deleting the newest record(s) | The issuer's last stored record must be the `aeat_chain` head unless the head is an anchor (`AeatChainCheck::verifyTail`) |
| A chain restart | Only the restart the client recorded in `aeat_chain_restarts` is accepted (README §The chain) |

Not detected: edits to `ingresos`, to the non-XML columns of `aeat_records`, or to the gateway's rows. AEAT holds the authoritative chain.

## 2. Numeración correlativa automática

> Art. 8.2.a / Orden HAC/1177/2024: series and continuous numbering, without gaps.

**COVERED.** Tests: `test_sql_lite.cpp`, `test_e2e_app.cpp`.

- No paid invoice is issued while Verifactu is not configured (`VerifactuIntegration::invoicingBlockedReason`: paid save, Cobrar, Rectificar, a paid garment in Añadir prendas; unpaid ones still save). With the direct client every save is also refused while its first chain check runs or has failed (`numberingBlockedReason`); every finished check refreshes the number on screen (`chainChecked` and the start-up sync, never in the middle of a save; Guardar is disabled while a save waits for AEAT).
- `MainWindow::setNextTicketNumber()` uses `sql_lite::nextTicketNumber(db, highestNumberAtAeat)`: the database's highest `n_recibo` + 1, or above the issuer's highest number at the real AEAT from any system (ours or the gateway's) when the database is behind it (restored from an older backup; direct client in production only: pre-production holds test numbers far above the shop's and never moves its numbering). The maximum is numeric (`MAX(CAST(n_recibo AS INTEGER))`; `n_recibo` is TEXT and a text maximum would repeat numbers past 99999). An unreadable highest number refuses to number (`nextTicketNumber` returns 0; saving, Rectificar and Facturas refuse) instead of falling back to 1.
- Rectificativas have their own series `R-1`, `R-2`… (art. 6.1.a RD 1619/2012): `sql_lite::nextRectificationNumber` = the highest `R-` number in `ingresos` + 1, or above `VerifactuIntegration::highestRectificationAtAeat` (direct client: the highest `R-` its store holds in the current environment and, in production, the highest at the real AEAT; with the gateway a restored database knows only its own numbers). Listings sort `R-` rows after the tickets (`N_RECIBO_ORDER`); `nextTicketNumber` ignores them.
- `n_recibo` is auto-filled (`resetAllContents()`) and not editable in the UI. One copy of the app runs per database (`InstanceLock`, `main.cpp`).
- Invoice sequence per payment event: `nextVerifactuInvoiceSeq` fails closed (an unreadable `ingresos` or `aeat_records` read means no charge).

## 3. Encadenamiento — huella criptográfica (SHA-256)

> Art. 12 RD 1007/2023, Anexo de la Orden HAC/1177/2024: each record carries a SHA-256 huella over key fields plus the previous record's huella.

**COVERED.** Tests: `test_aeat_hash.cpp` (official vectors), `test_aeat_chaincheck.cpp`, `test_aeat_direct_backend.cpp`, `test_aeat_response.cpp`, `test_verifactu_response.cpp`.

- **Direct client:** the app computes the huella (`AeatHash`) and keeps one chain per issuer and environment in `aeat_chain`, appended in one transaction with the record. The chain rules (one chain per SIF, sync with AEAT, restart as a new SIF, `NumeroInstalacion`) are in README §The chain; an unreadable chain is an error, never `PrimerRegistro`.
- **Gateway:** the chain is computed by AEAT / IreneSolutions. `parseVerifactuResponse()` extracts `<...:Huella>` from `Return.Xml` (any namespace prefix, upper-case hex) into `VerifactuResult::rawHash`, stored in `ingresos.verifactu_hash`.
- **Local tamper detection:** `AeatChainCheck` (requirement 1).

## 4. Conservación de registros

> Art. 8.2.c RD 1007/2023, Ley 58/2003 LGT: conserve the records for the prescription period, generally 4 years.

**COVERED.** Test: `test_backup_manager.cpp`. Full contract: `docs/modules/backup.md`.

- `ingresos` rows are never deleted by the application.
- `src/backup/BackupManager` snapshots the live DB with `VACUUM INTO` to `<dbDir>/backups/laideal_yyyy-MM-dd_HHmmss.db.part`, re-opens it read-only, validates `PRAGMA integrity_check` and only then renames it to `.db`. A failed verdict deletes the `.part` and shows the error, so a cut-off copy never counts as a backup. One run at a time; a run never removes a copy it did not create.
- Retention: every backup of the last 30 days; older ones reduced to one per calendar month for 4 years (the LGT prescription window), then deleted.
- Triggers: at app start once per 24 h (silent on success, red result-panel message on failure); on the first start of a new version, before `migrateDatabase()` changes the schema; on demand (**Hacer copia de seguridad ahora**).

## 5. Trazabilidad

> Art. 8.1 RD 1007/2023: each invoicing action traceable (who, when, from which terminal).

**COVERED, single-operator scope.**
- `verifactu_timestamp` records the AEAT submission time per row. "Who" is the single operator; the machine is fixed (one Windows account, one DB path).
- *Acerca de Verifactu* (the declaración responsable) states the **monoperador** scope and ties it to art. 8.1: per-user traceability is met implicitly by the single Windows session, and a second operator requires per-event identification first.
- If a second clerk is added this requirement re-opens: an operator prompt plus an `operador` column stamped on each `ingresos` write must ship before that account invoices.

## 6. Registro de eventos del sistema

> Art. 8.3 RD 1007/2023: the event log is **not** mandatory in VERI\*FACTU mode (AEAT keeps its own log through the submission stream); it is in NO-VERIFACTU mode.

**COVERED** (VERI\*FACTU). `AppLogger` writes `qDebug` / `qWarning` / `qCritical` to `~/.laideal.log` with rotation (5 MB), thread-safe. Switching to NO-VERIFACTU would need a structured event log (start / stop, errors, config changes).

## 7. Exportación y acceso de Hacienda

> Art. 14.1 RD 1007/2023: "acceso completo e inmediato" in legible form. Orden HAC/1177/2024: XML (UTF-8) with the AEAT schema.

**COVERED.** Tests: `test_e2e_app.cpp`, `test_sql_lite.cpp`.

Source: `verifactu_xml` holds the record AEAT registered (gateway: `Return.Xml`, extracted by `parseVerifactuResponse()`; direct client: the record the app built; a duplicate of another system's record is not claimed, so the column stays empty). `verifactu_cancel_xml` holds the cancellation record. With the direct client every generated record, accepted or not, is also kept in `aeat_records` with its exact XML.

**Facturas → Exportar registros AEAT (XML)…** (`AeatExportDialog`, `src/app/aeatexportdialog.cpp`):
- Window: Periodo (default the last three months), Archivo (default `<reports root>/AEAT/aeat_registros_<desde>_<hasta>.xml`), **Exportar registros**. Count, link to the file and any warning (empty period, undated invoices, write error) show in its result panel. An existing file is overwritten only after confirmation. A failed query shows an error instead of an empty file.
- Records come from `sql_lite::aeatExportRecords`; `writeAeatExportXml` (`src/app/aeatexport.cpp`) writes one envelope `<RegistrosFacturacionLaIdeal fechaDesde fechaHasta generadoEl nif emisor>`.
- One `<Registro nRecibo invoiceId fechaPago importe csv estado>` per **invoice AEAT holds**: per paid payment event `(n_recibo, verifactu_invoice_seq)` with a stored payload or CSV, whatever its estado (cancelled and rectified included), issued (`fecha_pago`) or cancelled (`fecha_anulacion`) in the range. `invoiceId` is the literal AEAT InvoiceID (`<n>` or `<n>-<seq>`), `importe` the event total.
- A cancelled invoice also appears as `<Anulacion nRecibo invoiceId fechaPago fechaAnulacion>` in the period it was made, with AEAT's cancellation record inlined; `local="1"` when it is an `<AnulacionLocal>` note (cancelled in the books, never sent).
- Optional attributes: `entorno` (the environment from the record's QR host, on both elements); `fechaAnulacion`; `rectifica` + `tipoRectificacion`; `fechaExpedicion` (when the payload's invoice date differs from the payment date: older invoices sent under the reception date); `sinPayload="1"` (known only by CSV, e.g. recovered with Consultar en AEAT; or a cancellation made before its record was stored); `sinFecha="1"` (cancellation without a date, written after its `<Registro>`); `payloadComoTexto="1"` (a payload that cannot be inlined: not well-formed, a DOCTYPE, an undeclared namespace prefix; written as escaped text so the file always parses).
- Each payload is inlined once, its `<?xml ?>` declaration stripped. Invoices AEAT holds without a readable payment date belong to no period; their number is shown in the result message.
- **Scope** (owner decision): what `ingresos` holds, i.e. invoices whose AEAT answer was written back. Direct-client records generated but not accepted (pending or refused) live only in `aeat_records` and are not exported: Hacienda is given what AEAT holds.

## 8. Declaración responsable del fabricante

> Art. 13 RD 1007/2023: the producer certifies compliance "por escrito y de modo visible en el propio sistema informático en cada una de sus versiones", with its identifying and location data. AEAT confirmed (13-10-2026) that the producer signs it even for own use.

**COVERED, pending the producer's signature data.** Tests: `test_aeat_record.cpp`, `test_selfcheck.cpp`.

- Content per art. 15 of the Orden, built by `ProducerDeclaration::html` (`src/verifactu/producerdeclaration.*`): the title "DECLARACIÓN RESPONSABLE DEL SISTEMA INFORMÁTICO DE FACTURACIÓN", then items a) to l) in order, each with its description (name La Ideal, code LI, version `PROJECT_VERSION`, components, only VERI\*FACTU = Sí, several obligados tributarios = No, signature types not applicable, the producer's name, NIF and postal address as in `AeatRecord::laIdealSystem`, the compliance statement of 15.1.k, date and place of signature), plus an annex (15.2.d) with the single-operator scope that closes requirement 5.
- Items j) and l) come from `ProducerDeclaration::currentSignature()`, filled in when the producer signs each version; until then they read "(pendiente)" and the window says the declaración is pending. The signature date is set on release day (`/release`).
- **Ayuda → Acerca de Verifactu** shows it inside the program (15.3) and saves it as PDF ("Guardar como PDF…"). With the gateway the window notes that their service is another producer's component with its own declaración (15.4).

## 9. QR y texto obligatorio en el ticket

> Disposición Final Primera RD 1007/2023, Orden HAC/1177/2024: the QR encodes a `TIKE-CONT/ValidarQR` URL (`nif`, `numserie`, `fecha`, `importe`); the receipt shows "VERI\*FACTU" or "Factura verificable en la sede electrónica de la AEAT" next to it.

**COVERED.** Tests: `test_escpos.cpp` (`test_renderLeyendaIsTheExactPhrase`), `test_imprimir.cpp`, `test_ticket_preview.cpp`, `test_aeat_qr.cpp`.

QR:
- At issue it comes with the answer (`result.qrCode`: the gateway's image, or drawn locally by `AeatQr` with the direct client, exactly level M). A reprint draws it locally from the stored `verifactu_url_qr` with either connection; the gateway's `/GetQrCode` is asked only when no URL was stored. The URL points to the validation host of the environment (README §Environments).
- `Imprimir::buildTicket()` rasters it into the ESC/POS receipt (`GS v 0`). `TicketRenderer::printableQr` reads the module grid back and redraws it at a whole number of dots per module, 30-40 mm as art. 21.1 requires (a QR it cannot read is printed as received, scaled down only if wider than the paper).
- Imprimir stays open between prints, so it drops the QR fetched for the previous print at every press (and for each invoice of "Todas las facturas"): a QR is only ever the one of the invoice being printed.

Leyenda (art. 20.1.b / 21):
- `Factura verificable en la sede electrónica de la AEAT`, exact long form, centred right below the QR in Font A on two lines that fit the 58 mm roll (`TicketRenderer`).
- Printed only when the invoice has at least one paid row (`pagado = 'SI'`, the rows the QR amount and printed lines use) and **every** paid row is `ENVIADA`, or `PENDIENTE` for a record the direct client has already issued or queued (below). Unpaid rows on the ticket are ignored. So the claim never appears on an invoice AEAT has not accepted or that is not queued.
- A reprint of an `ANULADA` or `RECTIFICADA` invoice is marked `*** FACTURA ANULADA / RECTIFICADA ***` ("esta copia no es una factura en vigor"), never with QR or leyenda (`TicketRenderer` and `resolveQrCode` both enforce it).
- Every factura prints "Fecha de expedición", the paid rows' `fecha_pago`: the date the QR and the AEAT record carry (art. 7 RD 1619/2012). A complete invoice needs the recipient's address; without it the event is not printed.

Direct client, before AEAT answers (the wait rules are in README §Waits and in-flight requests):
- `VerifactuIntegration::qrBeforeReply` returns the QR only once the record is generated, chained and queued, drawn from the stored record (`AeatDirectBackend::queuedRecordQr`: still pending, this issuer, same date and amount, never for a stale record under a reused number). That print carries the leyenda while the row is `PENDIENTE` (`Imprimir::verifactuIssued`), as a VERI\*FACTU invoice does whether or not AEAT has answered. A reprint before the answer draws the same QR (`Imprimir::resolveQrCode` -> `qrBeforeReply`); with the gateway a `PENDIENTE` invoice has no QR until its CSV.
- A refusal arriving after the print is shown in the result panel and counted in the status bar until resent with the same data (a refused rectificativa is redone in Rectificar factura under its own number); the count opens *Facturas rechazadas*. Refusals whose earliest payment is before the recovery floor (*Solo tickets desde*) are left out: they are pre-Verifactu tickets AEAT refuses for their old date. The floor must never be moved past a real production invoice (a check of the production switch).

## 10. Software de "doble uso" prohibido

> Art. 29.2.j) LGT (Ley 11/2021): software enabling hidden sales, "caja B" or silent record alteration is prohibited.

**COVERED.** Tests: `test_selfcheck.cpp`, `test_e2e_app.cpp`.

- No hidden-sales or off-the-books mode, no silent delete, no setting to skip AEAT submission per ticket.
- The accounting lock (`accounting_closures`, append-only with its close / reopen history) prevents modifying closed quarters; closing a quarter writes nothing to the invoice rows.
- No tool rewrites invoice rows. The decimal comma is replaced only in `gastos` outside closed quarters and in `prendas`; a new `hash` is given only to rows without a CSV whose hash was empty or shared with a sibling of the same ticket (each change logged with old and new value), inside `migrateDatabase`.
- **Post-update self-check** (`laideal.exe --self-check`, shipped in releases; `docs/testing/smoke_test.md` part 2): no MainWindow and no invoicing. It works on a temporary copy of the newest backup with a settings copy (direct client in pre-production, no gateway key, no certificate password), deleted at the end together with a crashed earlier run's copies. It never builds `VerifactuIntegration`, so it creates, numbers or sends no record. It reprints only an invoice whose paid rows are all `ENVIADA` in the copy and still `ENVIADA` in the live DB (opened read-only), with the QR drawn from its stored URL. The prompt tells the user the papers are test prints.
- **Test-only build:** CMake option `LAIDEAL_UI_TEST` (OFF by default) compiles a `--fake-aeat http://127.0.0.1:<port>` switch for the UI smoke test (`docs/testing/ui_smoke.md`). It never ships: `releases/package.ps1` (CI release job and `release.ps1`) refuses an exe holding the `--fake-aeat` marker, the `ui-smoke` job checks its own build holds it (so the check cannot go blind) and uploads no exe, configuring it ON prints a warning, and the in-app updater installs only GitHub Releases.

---

## Summary

| # | Requirement | Status |
|---|-------------|--------|
| 1 | Integrity / inalterabilidad | COVERED |
| 2 | Numeración correlativa | COVERED |
| 3 | Hash chain (SHA-256) | COVERED |
| 4 | Retention (4 years) | COVERED (`BackupManager`) |
| 5 | Trazabilidad | COVERED (monoperador scope, art. 8.1) |
| 6 | Event log | COVERED (VERI\*FACTU mode) |
| 7 | XML export for Hacienda | COVERED |
| 8 | Declaración responsable | COVERED once signed (signature date set on release day, `/release`) |
| 9 | QR + mandatory text | COVERED |
| 10 | No doble-uso software | COVERED |

A compliance gap found by an audit against these rules is filed in the Open Issues of [`docs/progress_tracker.md`](../../progress_tracker.md), names the requirement number above and gates the release. At present every requirement is COVERED.
