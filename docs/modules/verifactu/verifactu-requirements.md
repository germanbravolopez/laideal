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
- Cancellation flow: `CancelInvoiceDialog` → `cancelInvoiceAsync` → AEAT, sets `verifactu_estado = 'ANULADA'`. ✓
- Rectification flow: `RectifyInvoiceDialog` → `VerifactuIntegration::submitRectificationAsync()` → AEAT. Supports R1-R5 invoice types and both `RectificationType` variants (`S` = sustitución, `I` = diferencias). Substitution marks the original rows `verifactu_estado = 'RECTIFICADA'` so they are excluded from `totalPriceBetweenDates()` without losing the audit trail; the new rectificativa row links back via `verifactu_rectifies_n_recibo`. ✓
- Note: the DB is SQLite; nothing prevents an operator with file-system access from editing it directly. The regulation tolerates this (it requires *the software* to detect/warn, not the OS to prevent), and the per-row `verifactu_hash` SHA-256 chain (Req. 3) gives a tamper-detection signal on read.

## 2. Numeración correlativa automática

> Art. 8.2.a / Orden HAC/1177/2024. Cada factura/ticket debe llevar una serie y numeración continua, sin huecos.

**[COVERED]**
- `setNextTicketNumber()` in `MainWindow` uses `readMaxValueInColumnFromTable(db, "n_recibo", "ingresos") + 1` — strictly correlative; no way to skip a number.
- The user cannot edit `n_recibo` directly in the UI (the field is auto-filled on `resetAllContents()`).

## 3. Encadenamiento — huella criptográfica (SHA-256)

> Art. 12 RD 1007/2023, Anexo de la Orden HAC/1177/2024. Cada registro debe incluir una huella SHA-256 sobre campos clave + la huella del registro anterior, formando una cadena.

**[COVERED]**
- In VERIFACTU mode, the SHA-256 hash chain is computed and persisted by the AEAT/IreneSolutions side; the submission response includes the chained `Huella` value in the SOAP XML (e.g. `<sum1:Huella>4486F3131E8AAFA0559F7CB23A1605EC177A15B61F8C595ED2910599CEAC7FCB</sum1:Huella>`).
- La Ideal extracts `<...:Huella>...</...:Huella>` from `Return.Xml` in `VerifactuManager::processResponse()` (regex tolerant of any namespace prefix; upper-cased hex) into `VerifactuResult::rawHash`, and persists it to the new `verifactu_hash TEXT` column on `ingresos` via the existing async update path. ✓
- Local tamper-detection: holding the per-row chained hash lets an audit recompute the chain and detect any silent edit or insertion in `ingresos`, independently of AEAT.

## 4. Conservación de registros

> Art. 8.2.c RD 1007/2023, Ley 58/2003 LGT. Records must be conserved during the tax prescription period — generally 4 years, longer for some cases.

**[COVERED]**
- `ingresos` rows are never deleted by the application.
- `src/backup/BackupManager` snapshots the live SQLite DB via `VACUUM INTO` to `<dbDir>/backups/laideal_yyyy-MM-dd_HHmmss.db`. The copy is re-opened read-only and validated with `PRAGMA integrity_check`; a failed verdict deletes the copy and surfaces the error.
- Retention: every backup from the last 30 days is kept; older backups are reduced to one per calendar month for 4 years (matching the LGT prescription window), then deleted.
- Triggers: (1) auto on app startup once per 24h (silent on success, `QMessageBox` warning on failure); (2) on demand via **Herramientas → Hacer copia de seguridad ahora...**.
- See `docs/modules/backup.md` for the full contract.

## 5. Trazabilidad

> Art. 8.1 RD 1007/2023. Each invoicing action must be traceable: who, when, from which terminal.

**[COVERED — single-operator scope]**
- `verifactu_timestamp` records the AEAT submission time per row. ✓
- "Who" is the single operator running the shop; the machine identity is fixed (hardcoded DB path on one Windows account).
- The `Acerca de Verifactu...` declaración responsable now states the **monoperador** scope explicitly and ties it to Art. 8.1: trazabilidad por usuario is satisfied implicitly by the single Windows session, and the declaration calls out that incorporating a second operator requires per-event identification first.
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
- New action `MainWindow → Herramientas → Exportar registros AEAT (XML)...` (`on_actionExportar_registros_aeat_triggered()`) prompts for a date range and output path, then writes a single envelope file `<RegistrosFacturacionLaIdeal fechaDesde="…" fechaHasta="…" generadoEl="…" nif="…" emisor="…">` containing one `<Registro nRecibo="…" fechaRecepcion="…" importe="…" csv="…">` element per qualifying ticket (estado=ENVIADA AND non-empty XML AND fecha_recepcion in range). The stored payload is inlined with its `<?xml ?>` declaration stripped so the outer document stays well-formed. ✓
- Caveat: tickets submitted before this feature landed do not have `verifactu_xml` stored (no retroactive AEAT lookup); they are silently skipped from the export.

## 8. Declaración responsable del fabricante

> Art. 13 RD 1007/2023. The producer must include "una declaración responsable" by which they certify compliance with the regulation. It must be "por escrito y de modo visible en el propio sistema informático en cada una de sus versiones" and include the producer's identifying and location data.

**[COVERED]**
- `MainWindow → Ayuda → Acerca de Verifactu...` opens a dialog (`on_actionAcerca_de_Verifactu_triggered()` in `mainwindow.cpp`) with the fixed-text declaración responsable.
- Producer NIF, name and address come from `AppSettings::verifactuNif()`, `verifactuName()` (falling back to `businessName()`), `businessAddress()` and `businessCity()` — the user-business doubles as producer for this bespoke deployment.
- Software name (`La Ideal`) and version are interpolated from `PROJECT_VERSION_MAJOR/MINOR` (generated `version.h`), so the declaration is automatically per-version as required by Art. 13.
- The body cites RD 1007/2023 and Orden HAC/1177/2024 explicitly, states VERI*FACTU mode, and (since 8.5) declares the **monoperador** scope that closes Req. 5 (Art. 8.1 trazabilidad) under the current deployment.

## 9. QR y texto obligatorio en el ticket

> Disposición Final Primera RD 1007/2023, Orden HAC/1177/2024. The QR encodes a URL to `TIKE-CONT/ValidarQR` with params `nif`, `numserie`, `fecha`, `importe`. The receipt must also display the **mandatory text** "VERI\*FACTU" or "Factura verificable en la sede electrónica de la AEAT" next to the QR.

**[COVERED]**
- QR is generated by Irene Solutions from the response (`result.qrCode`) and embedded by `Imprimir::createTicketExcel()` at 140×140 px. URL points to the correct AEAT endpoint per environment (`TESTING` vs `PRODUCTION`, see `VerifactuConfig::getValidationUrl()`). ✓
- Verification leyenda `Factura verificable en la sede electrónica de la AEAT` is now written immediately below the QR by `Imprimir::createTicketExcel()`, centred across columns A:C at font 7. Guarded by `verifactu_estado == "ENVIADA"` (via `verifactuEstadoToString(VerifactuEstado::Enviada)`) so the claim never appears on PENDIENTE / ERROR / ANULADA rows. ✓

## 10. Software de "doble uso" prohibido

> Art. 29.2.j) LGT (introduced by Ley 11/2021). Software that enables hidden sales, off-the-books bookkeeping ("caja B"), silent record alteration, etc. is prohibited.

**[COVERED]**
- La Ideal has no features for hidden sales, no "off the books" mode, no silent delete, no setting to disable AEAT submission per ticket.
- The accounting lock (`edit_lock`) prevents modification of closed quarters.
- `cleanDatabase()` only normalises decimal separators (comma→dot); it doesn't alter amounts or hide records.

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
| 8 | Declaración responsable | COVERED |
| 9 | QR + mandatory text | COVERED |
| 10 | No doble-uso software | COVERED |

**Blocking issues filed** (one per non-covered gap) — see `docs/progress_tracker.md`. As of 2026-05-31, every Verifactu compliance requirement is COVERED. Req. 4 was closed via the new `BackupManager` module; Req. 5 was closed by stating the monoperador scope in the declaración responsable. If the shop later adds a second clerk, Req. 5 re-opens and the full per-operator-stamping fix needs to ship before that second account starts invoicing.
