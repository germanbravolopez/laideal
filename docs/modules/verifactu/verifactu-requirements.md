# Verifactu / SIF — Legal Requirements vs. La Ideal Coverage

**Mode**: La Ideal operates in **VERIFACTU** mode (submits each invoice record to AEAT immediately). This is the lighter of the two modes — the alternative ("NO VERIFACTU") additionally requires electronic signing of every record and a richer event log.

**Sources (verified 2026-05-25)**:
- [Real Decreto 1007/2023, de 5 de diciembre (BOE-A-2023-24840)](https://www.boe.es/buscar/act.php?id=BOE-A-2023-24840) — the regulation itself.
- [Orden HAC/1177/2024, de 17 de octubre (BOE-A-2024-22138)](https://www.boe.es/buscar/act.php?id=BOE-A-2024-22138) — technical specifications.
- [Ley 11/2021 (Ley Antifraude)](https://www.boe.es/buscar/act.php?id=BOE-A-2021-11473) — article 29.2.j) LGT, the parent law.
- [AEAT — Sistemas Informáticos de Facturación y VERI\*FACTU](https://sede.agenciatributaria.gob.es/Sede/iva/sistemas-informaticos-facturacion-verifactu.html).

Status legend: **[COVERED]** = fully satisfied · **[PARTIAL]** = partially satisfied or relies on a third-party component · **[MISSING]** = not satisfied; a blocking issue is filed in `docs/progress_tracker.md`.

---

## 1. Integridad e inalterabilidad de los registros

> Art. 8.1 RD 1007/2023. Los registros "no puedan ser alterados sin que el sistema informático lo detecte y avise de ello." Si hay un error, debe corregirse mediante **anulación** o **factura rectificativa** — nunca borrando.

**[COVERED]**
- `Listado::on_actionEliminar_fila_triggered()` blocks row deletion when `tableName == "ingresos"` (the invoices table). No UI path can delete a submitted invoice. ✓
- `Listado::populateTable()` calls `setEditTriggers(NoEditTriggers)` on the `ingresos` view, so no UI path can inline-edit any column either - all changes must flow through RecogPrendas / CancelInvoiceDialog / RectifyInvoiceDialog, which keep AEAT and the accounting lock in sync. ✓
- Insert-and-submit paths are ticket save (MainWindow), Cobrar (PayDialog), Añadir nuevas prendas with a paid garment (from the menu or Recogida) (AddGarment → MainWindow, only on a ticket with no paid garment, re-checked at save) and the rectificativa; a reply is written back by `updateTicketVerifactuFields`, which never rewrites a row already `ENVIADA`, `ANULADA` or `RECTIFICADA` (and `markTicketRectified` never marks a row already `ANULADA` or `RECTIFICADA`; a ticket with a rectificativa still `PENDIENTE` cannot be rectified or cancelled again) (a late duplicate rejection cannot wipe a registered invoice's CSV / payload / Huella).
- An m2 garment without its size (`garmentUnmeasured`) is never invoiced: the main window refuses saving such a ticket as paid and Cobrar refuses charging it, because the invoice would reach AEAT at 0 with its amounts frozen. A ticket saved as paid sends exactly the sum of the rows it stored; a negative hand-typed price is refused.
- The amount of a paid / sent row cannot be edited: the `sql_lite` amount writers (size, quantity, service, manual price) refuse it themselves, besides the read-only Recogida fields. From 11.0 every amount is stored in cents, the value charged and sent (`moneyText`); open amounts stored earlier with three decimals are rounded by `migrateDatabase` (in the shop only from 11.2: the 11.0 / 11.1 guard compared the TEXT `edit_lock` wrongly and matched nothing), paid ones (already sent) are kept as stored.
- Separar prendas (Recogida) on a paid row moves garments to a new row that copies the original's `verifactu_*` columns, giving the split-off part its `importe` in cents and the original the exact remainder, so the invoice's rows keep the stored total (`sql_lite::splitGarmentRow`; refused on a row locked by Contabilidad).
- Cancellation flow: `CancelInvoiceDialog` → `cancelInvoiceAsync` → AEAT, one request per payment event, under the date the invoice was registered with (its earliest `fecha_pago`, via `verifactuEventFor`; an old seq-0 invoice rejected under it is retried once with the reception date, which pre-10.9 resubmissions used), then `sql_lite::markInvoiceSeqCancelled` sets `verifactu_estado = 'ANULADA'` on that payment event's paid rows (and records `fecha_anulacion`, so the payment period keeps its income, a filed Contabilidad report is never altered, and the cancellation is accounted in the period it happens; refused while the current quarter is closed). ✓
- Rectification flow: `RectifyInvoiceDialog` → `VerifactuIntegration::submitRectificationAsync()` → AEAT. Supports R1-R5 invoice types and both `RectificationType` variants (`S` = sustitución, `I` = diferencias). Substitution marks the original's paid rows `verifactu_estado = 'RECTIFICADA'` via `sql_lite::markTicketRectified` (recording `fecha_anulacion` = the rectificativa's date: the original's payment period keeps it and the rectificativa's period subtracts it; the rectificativa may not be dated in a closed quarter nor before the original payment; a refused one counted in a quarter closed since is resent only as it was filed - its date, amount and S/I mode - its row untouched but PENDIENTE while sent, and the original is then marked in an open period (`openPeriodDate`: that date, else today, else the next open quarter)) so (from 10.12, with `fecha_anulacion`) they stay income in their payment period and are subtracted in the rectificativa's period, without losing the audit trail; the new rectificativa row links back via `verifactu_rectifies_n_recibo`. ✓
- Database guards (`migrateDatabase`, since October 2026): two triggers refuse deleting an `ingresos` row registered at AEAT (a CSV, or ENVIADA / ANULADA / RECTIFICADA) and changing an ENVIADA row's invoice identity (`n_recibo`, `fecha_pago`, `pagado`, CSV, XML, `verifactu_hash`, InvoiceID, seq). The estado, the cancellation date, the pickup and the amount (Separar prendas keeps the invoice total over the two parts) still move. A bug in the application can no longer silently alter or remove an invoice row.
- Note: the DB is SQLite; nothing prevents an operator with file-system access from editing it directly. The regulation tolerates this (it requires *the software* to detect/warn, not the OS to prevent). With the direct AEAT client part of it is detected: `AeatChainCheck::verify` recomputes the huella of every stored `aeat_records` record from its XML and checks each link to the previous record, per issuer. It runs at every start (`AeatDirectBackend::verifyStoredChain`, a red result-panel message and a `qCritical` on failure) and in the post-update self-check (M1c). It detects an alteration of a stored record's hashed fields (number, date, type, amounts, links), a reordering and a restarted chain. It does not detect a deleted record: the next one then links to a huella this database does not hold, which is also what a restored copy continuing a record only AEAT has looks like, so it is only logged. Edits to `ingresos`, to the non-XML columns of `aeat_records` or to the gateway's rows are not detected either. AEAT holds the authoritative chain.
- A cancellation whose reply was lost is resent as the same record; AEAT's duplicate answer ("already cancelled") settles it as cancelled (`AeatDirectBackend`), so the books and AEAT agree and Anular does not loop.

## 2. Numeración correlativa automática

> Art. 8.2.a / Orden HAC/1177/2024. Cada factura/ticket debe llevar una serie y numeración continua, sin huecos.

**[COVERED]**
- No paid invoice is issued while Verifactu is not configured (`VerifactuIntegration::invoicingBlockedReason`: paid save, Cobrar, Rectificar, a paid garment in Añadir prendas; unpaid ones are still saved). With the direct client, every save is refused while its first chain check is running or has failed (`numberingBlockedReason`), and every finished check refreshes the number on screen (`chainChecked` and the start-up sync, never in the middle of a save; Guardar is disabled while a save waits for AEAT).
- `setNextTicketNumber()` in `MainWindow` (and Rectificar factura) uses `sql_lite::nextTicketNumber(db, highestNumberAtAeat)`: the database's highest `n_recibo` + 1, or above the issuer's highest number at the real AEAT, from any system (ours or the gateway's), when the database is behind it (restored from an older backup; direct client in production only - pre-production holds test numbers far above the shop's and never moves its numbering) — strictly correlative; no way to skip or repeat a number. The maximum is numeric (`MAX(CAST(n_recibo AS INTEGER))` since 11.2): `n_recibo` is TEXT, and a text maximum would repeat numbers past 99999.
- The user cannot edit `n_recibo` directly in the UI (the field is auto-filled on `resetAllContents()`).
- Numbering also relies on one copy of the app per database (`InstanceLock`, `main.cpp`), and an unreadable highest number refuses to number (`nextTicketNumber` returns 0; saving, Rectificar and Facturas refuse) instead of falling back to 1.

## 3. Encadenamiento — huella criptográfica (SHA-256)

> Art. 12 RD 1007/2023, Anexo de la Orden HAC/1177/2024. Cada registro debe incluir una huella SHA-256 sobre campos clave + la huella del registro anterior, formando una cadena.

**[COVERED]**
- In VERIFACTU mode, the SHA-256 hash chain is computed and persisted by the AEAT/IreneSolutions side; the submission response includes the chained `Huella` value in the SOAP XML (e.g. `<sum1:Huella>4486F3131E8AAFA0559F7CB23A1605EC177A15B61F8C595ED2910599CEAC7FCB</sum1:Huella>`).
- La Ideal extracts `<...:Huella>...</...:Huella>` from `Return.Xml` in `VerifactuManager::processResponse()` (regex tolerant of any namespace prefix; upper-cased hex) into `VerifactuResult::rawHash`, and persists it to the new `verifactu_hash TEXT` column on `ingresos` via the existing async update path. ✓
- Local tamper-detection (direct client): `AeatChainCheck` recomputes the stored chain at start and in the self-check (see Req. 1). An edit of a record's amounts, date or number, a reordering, or a second `PrimerRegistro` is reported; a deletion is not (see Req. 1).

- *Direct AEAT client (the 12.0 connection; the gateway stays selectable):* the app computes the huella itself (`AeatHash`, official vectors) and keeps one chain per issuer and environment in `aeat_chain`, appended in one transaction with the record; the chain only continues La Ideal's own records (the local store and AEAT's records of our `SistemaInformatico`; never the gateway's, one chain per SIF and obligado tributario as AEAT answered on 13-10-2026) and starts with `PrimerRegistro` when there are none; it is synced with AEAT at each start (a database restored from an older backup continues where AEAT is, and the next ticket number starts above AEAT's); a database whose chain differs from the marker this installation keeps in its settings (`verifactu.aeat_chain_marker`) must reach AEAT first, never falling back to its stale chain and no record is generated before the first sync; an unreadable chain is an error, never `PrimerRegistro`. See `docs/modules/verifactu/aeat-direct-investigation.md`.

## 4. Conservación de registros

> Art. 8.2.c RD 1007/2023, Ley 58/2003 LGT. Records must be conserved during the tax prescription period — generally 4 years, longer for some cases.

**[COVERED]**
- `ingresos` rows are never deleted by the application.
- `src/backup/BackupManager` snapshots the live SQLite DB via `VACUUM INTO` to `<dbDir>/backups/laideal_yyyy-MM-dd_HHmmss.db.part`. The copy is re-opened read-only and validated with `PRAGMA integrity_check`, and only then renamed to its final `.db` name. A failed verdict deletes the `.part` and surfaces the error, so a cut-off copy never counts as a backup. A run never removes a copy it did not create, and only one runs at a time.
- Retention: every backup from the last 30 days is kept; older backups are reduced to one per calendar month for 4 years (matching the LGT prescription window), then deleted.
- Triggers: (1) auto on app startup once per 24h (silent on success, a red result-panel message on failure); (2) on the first start of a new version, before `migrateDatabase()` changes the schema; (3) on demand via **Hacer copia de seguridad ahora**.
- See `docs/modules/backup.md` for the full contract.

## 5. Trazabilidad

> Art. 8.1 RD 1007/2023. Each invoicing action must be traceable: who, when, from which terminal.

**[COVERED — single-operator scope]**
- `verifactu_timestamp` records the AEAT submission time per row. ✓
- "Who" is the single operator running the shop; the machine identity is fixed (hardcoded DB path on one Windows account).
- The `Acerca de Verifactu` declaración responsable now states the **monoperador** scope explicitly and ties it to Art. 8.1: trazabilidad por usuario is satisfied implicitly by the single Windows session, and the declaration calls out that incorporating a second operator requires per-event identification first.
- If a second clerk is ever added, this requirement re-opens and the original full-fix plan (operator code prompt + new `operador TEXT` column stamped on each `ingresos` write) becomes the path forward.

## 6. Registro de eventos del sistema

> Art. 8.3 RD 1007/2023. **In VERIFACTU mode**, the event log is *not* mandatory (AEAT keeps its own log via the submission stream). **In NO-VERIFACTU mode** it is. Since La Ideal is VERIFACTU, this is informational.

**[COVERED]** (for VERIFACTU mode)
- `AppLogger` captures `qDebug`/`qWarning`/`qCritical` to `~/.laideal.log` with rotation (5 MB), thread-safe via QMutex. Sufficient for technical troubleshooting.
- Note: if La Ideal ever switches to NO-VERIFACTU mode, this becomes insufficient — a structured event log with start/stop, errors, config changes, etc. would be required.

## 7. Exportación y acceso de Hacienda

> Art. 14.1 RD 1007/2023. The Administration can require "acceso completo e inmediato" in legible format. Orden HAC/1177/2024 fixes XML (UTF-8) as the export format with the AEAT schema.

**[COVERED]**
- The AEAT-style XML returned by Irene Solutions in `Return.Xml` of every `/Create` response is extracted in `VerifactuManager::processResponse()` into `VerifactuResult::rawXml` and persisted to a `verifactu_xml TEXT` column on `ingresos` (added in `migrateDatabase()`). Initial INSERT writes empty, the async `updateTicketVerifactuFields()` patches the column when AEAT replies. ✓
- Action `MainWindow → Facturas → Exportar registros AEAT (XML)...` (`AeatExportDialog`) asks for a date range and output file in its own window and reports the result there, then writes a single envelope file `<RegistrosFacturacionLaIdeal fechaDesde="…" fechaHasta="…" generadoEl="…" nif="…" emisor="…">` containing one `<Registro nRecibo="…" invoiceId="…" fechaPago="…" importe="…" csv="…" estado="…">` element per **invoice AEAT holds**, i.e. per paid payment event `(n_recibo, verifactu_invoice_seq)` with a stored payload or a CSV, whatever its current estado (cancelled and rectified invoices included), issued (`fecha_pago`) or cancelled (`fecha_anulacion`) in the range. Optional attributes: `fechaAnulacion` (cancelled / substituted), `rectifica` + `tipoRectificacion` (a rectificativa and the ticket it corrects), `sinPayload="1"` (known only by its CSV, e.g. recovered with "Consultar en AEAT", which stores no payload). A `<Registro>` appears in the period the invoice was issued; a cancellation (`ANULADA`) also appears as `<Anulacion invoiceId fechaPago fechaAnulacion>` in the period it was made, with AEAT's cancellation record (`verifactu_cancel_xml`) inlined, or `sinPayload="1"` for cancellations made before it was stored; one made before 10.12 has no date, so it is written right after its `<Registro>` with `sinFecha="1"`. `fechaExpedicion` is added to either element when the stored payload names another invoice date than the payment (the first `FechaExpedicionFactura` / `FechaExpedicionFacturaAnulada`: older invoices sent under the reception date). A payload that cannot be inlined (not well-formed, a DOCTYPE, an undeclared namespace prefix) is written as escaped text with `payloadComoTexto="1"`, so the file always parses. Invoices AEAT holds without a readable payment date belong to no period; their number is shown in the result message. A failed query shows an error instead of writing an empty file. `invoiceId` is the literal AEAT InvoiceID (`<n>` or `<n>-<seq>`), `importe` the event total. The records come from `sql_lite::aeatExportRecords`; `writeAeatExportXml` (`src/app/aeatexport.cpp`) writes the file, inlining each payload once with its `<?xml ?>` declaration stripped so the outer document stays well-formed. ✓ (until October 2026 it wrote one element per garment row, with the bare `n_recibo` and filtered by reception date.)
- Caveats: tickets submitted before the payload was stored, and invoices recovered with "Consultar en AEAT", have no `verifactu_xml`; they are exported with their CSV and `sinPayload="1"`. Likewise cancellations made before October 2026 have no stored cancellation record (`<Anulacion sinPayload="1">`).

- *Direct AEAT client:* `verifactu_xml` / `verifactu_hash` hold the record the app built and AEAT registered; when AEAT already held another system's record for the invoice (duplicate), ours is not claimed and the columns stay empty. Every generated record, accepted or not, is also kept in `aeat_records` with its exact XML.

## 8. Declaración responsable del fabricante

> Art. 13 RD 1007/2023. The producer must include "una declaración responsable" by which they certify compliance with the regulation. It must be "por escrito y de modo visible en el propio sistema informático en cada una de sus versiones" and include the producer's identifying and location data.

**[COVERED, pending the producer's signature data]** (rewritten for 12.0 after AEAT confirmed on 13-10-2026 that the producer signs it even for own use)
- Content per art. 15 of Orden HAC/1177/2024, built by `ProducerDeclaration::html` (`src/verifactu/producerdeclaration.*`): the title "DECLARACIÓN RESPONSABLE DEL SISTEMA INFORMÁTICO DE FACTURACIÓN", then items a) to l) in order, each preceded by its description - name La Ideal, code LI, the version (`PROJECT_VERSION`), components and description, only VERI\*FACTU = Sí, several obligados tributarios = No, signature types = not applicable, producer Germán Bravo López and NIF 75929840D (the same `AeatRecord::laIdealSystem` the records carry), his postal address, the compliance statement of 15.1.k, and the date and place of signature - plus an annex (15.2.d) with the single-operator scope that closes Req. 5 (art. 8.1).
- Items j) and l) come from `ProducerDeclaration::currentSignature()`, filled in when the producer signs each version; until then they read "(pendiente)" and the window says the declaración is pending.
- `MainWindow → Ayuda → Acerca de Verifactu` shows it inside the program (15.3) and saves it as PDF for the client ("Guardar como PDF..."). With the IreneSolutions connection the window notes that their service is another producer's component with its own declaración (15.4).
- Until 11.2 the dialog named the issuer (the shop) as producer and missed most items.

## 9. QR y texto obligatorio en el ticket

> Disposición Final Primera RD 1007/2023, Orden HAC/1177/2024. The QR encodes a URL to `TIKE-CONT/ValidarQR` with params `nif`, `numserie`, `fecha`, `importe`. The receipt must also display the **mandatory text** "VERI\*FACTU" or "Factura verificable en la sede electrónica de la AEAT" next to the QR.

**[COVERED]**
- QR is generated by Irene Solutions from the response (`result.qrCode`) and rastered into the ESC/POS receipt by `Imprimir::buildTicket()` (`GS v 0`). `TicketRenderer::printableQr` reads the symbol's module grid back from the pixmap and redraws the same modules at a whole number of dots each, so it measures 30-40 mm as art. 21.1 of the Orden requires (until 12.0 it was scaled smoothly to 192 dots, about 24 mm). URL points to the correct AEAT endpoint per environment (`TESTING` vs `PRODUCTION`, see `VerifactuConfig::getValidationUrl()`). ✓
- Verification leyenda `Factura verificable en la sede electrónica de la AEAT` is printed immediately below the QR by `TicketRenderer` (centred, in Font A like the rest of the invoice as art. 20.1.b requires, on two lines that fit the 58 mm roll; Font B, the smallest, until 12.0). Guarded by `verifactu_estado == "ENVIADA"` (via `verifactuEstadoToString(VerifactuEstado::Enviada)`) so the claim never appears on PENDIENTE / ERROR / ANULADA rows. A reprint of an ANULADA or RECTIFICADA invoice is printed marked `*** FACTURA ANULADA / RECTIFICADA ***` ("esta copia no es una factura en vigor"), never with a QR or leyenda; `TicketRenderer` enforces it as well as `resolveQrCode`. Every factura prints "Fecha de expedición", the paid rows' `fecha_pago`: the same date the QR and the AEAT record carry (art. 7 RD 1619/2012). Since 11.0 the Imprimir window stays open between prints, so it drops the QR fetched for the previous print at every press (and for each invoice of "Todas las facturas"): a QR is only ever the one of the invoice being printed. ✓

- *Direct AEAT client:* the QR is drawn locally (`AeatQr`, vendored qrcodegen, exactly level M) from the same verification URL. At issue (ticket paid at reception, Cobrar) the factura prints at once, without waiting for AEAT (`TiempoEsperaEnvio` can hold the next submission 60 s): `VerifactuIntegration::qrBeforeReply` returns the QR only once the record is generated, chained and queued, drawn from the stored record (`AeatDirectBackend::queuedRecordQr`: still pending, this issuer, same date and amount - never for a stale record under a reused number); that print carries the leyenda while the row is PENDIENTE (`Imprimir::verifactuIssued`), as a VERI\*FACTU invoice does whether or not AEAT has answered yet. A reprint before AEAT answers (Imprimir factura) draws the same queued record's QR with its leyenda (`Imprimir::resolveQrCode` → `qrBeforeReply`); with the gateway a PENDIENTE invoice still has no QR until its CSV. A refusal arriving after that print is shown in the result panel, and the main window's status bar keeps the count of refused invoices until they are resent with the same data (a refused rectificativa is redone in Rectificar factura under its own number). The count opens the list of them (*Facturas rechazadas*). Refusals whose earliest payment is before the recovery floor date (*Solo tickets desde*) are left out. Those are pre-Verifactu tickets AEAT refuses for their old date and will never accept. The floor must therefore never be moved past a real production invoice (a check of the production switch).

## 10. Software de "doble uso" prohibido

> Art. 29.2.j) LGT (introduced by Ley 11/2021). Software that enables hidden sales, off-the-books bookkeeping ("caja B"), silent record alteration, etc. is prohibited.

**[COVERED]**
- La Ideal has no features for hidden sales, no "off the books" mode, no silent delete, no setting to disable AEAT submission per ticket.
- The accounting lock (`accounting_closures`, append-only with its close / reopen history) prevents modification of closed quarters; closing a quarter writes nothing to the invoice rows.
- No tool rewrites invoice rows. The two legacy Archivo utilities went in 12.0: *Limpiar base de datos* also replaced the decimal comma in `ingresos.importe` of any row, sent invoices included, and *Crear hash en ingresos* rewrote `hash` across the table. Their remaining jobs run in `migrateDatabase` without touching invoices. The decimal comma is replaced only in `gastos` outside closed quarters and in `prendas`. A new `hash` is given only to rows without a CSV whose hash was empty or shared with a sibling of the same ticket (each change logged with the old and new value).
- **Post-update self-check** (`laideal.exe --self-check`, shipped in releases; `docs/testing/smoke_test.md` part 2): no MainWindow and no invoicing. It works on a temporary copy of the newest backup with a settings copy (direct client in pre-production, no gateway key, no certificate password) that is deleted at the end, together with a crashed earlier run's copies. It never builds `VerifactuIntegration`, so it creates, numbers or sends no record. It reprints only an invoice whose paid rows are all `ENVIADA` in the copy and still `ENVIADA` in the live DB (opened read-only), with the QR drawn from its stored verification URL. The question tells the user the papers are test prints, not for a customer.
- **Test-only build:** the CMake option `LAIDEAL_UI_TEST` (OFF by default) compiles a `--fake-aeat http://127.0.0.1:<port>` switch for the automated UI smoke test (`docs/testing/ui_smoke.md`), with which a local fake AEAT "accepts" the records. It never ships: the CI release job and `release.ps1` refuse an exe holding the `--fake-aeat` marker, `ui-smoke.yml` checks that its own build holds it (so the check cannot go blind) and uploads no exe, configuring it ON prints a warning, and the in-app updater installs only GitHub Releases.

---

## Summary

| # | Requirement | Status |
|---|-------------|--------|
| 1 | Integrity / inalterabilidad | COVERED |
| 2 | Numeración correlativa | COVERED |
| 3 | Hash chain (SHA-256) | COVERED |
| 4 | Retention (4 años) | COVERED — `BackupManager` (VACUUM INTO + integrity_check + 30-day daily / 4-year monthly retention; auto + manual triggers) |
| 5 | Trazabilidad | COVERED — monoperador scope stated in the declaración responsable (Art. 8.1) |
| 6 | Event log | COVERED (VERIFACTU mode) |
| 7 | XML export for Hacienda | COVERED |
| 8 | Declaración responsable | COVERED once signed: the signature date is set on release day (`/release`), still pending for 12.0 |
| 9 | QR + mandatory text | COVERED |
| 10 | No doble-uso software | COVERED |

**Blocking issues filed** (one per non-covered gap) — see `docs/progress_tracker.md`. As of 2026-05-31, every Verifactu compliance requirement is COVERED. Req. 4 was closed via the new `BackupManager` module; Req. 5 was closed by stating the monoperador scope in the declaración responsable. If the shop later adds a second clerk, Req. 5 re-opens and the full per-operator-stamping fix needs to ship before that second account starts invoicing.
