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

**[PARTIAL]**
- `Listado::on_actionEliminar_fila_triggered()` blocks row deletion when `tableName == "ingresos"` (the invoices table). No UI path can delete a submitted invoice. ✓
- Cancellation flow exists: `CancelInvoiceDialog` → `cancelInvoiceAsync` → AEAT, sets `verifactu_estado = 'ANULADA'`. ✓
- **Gap**: there is no UI for **factura rectificativa** (R1–R5 corrective invoices); already tracked as a Non-Blocking issue but should be promoted to Blocking since the law mandates it as one of only two legal correction paths.
- **Gap**: the DB is SQLite; nothing prevents an operator with file-system access from editing it directly. The regulation tolerates this (it requires *the software* to detect/warn, not the OS to prevent), but row-level integrity could be strengthened with a per-row hash check on read.

## 2. Numeración correlativa automática

> Art. 8.2.a / Orden HAC/1177/2024. Cada factura/ticket debe llevar una serie y numeración continua, sin huecos.

**[COVERED]**
- `setNextTicketNumber()` in `MainWindow` uses `readMaxValueInColumnFromTable(db, "n_recibo", "ingresos") + 1` — strictly correlative; no way to skip a number.
- The user cannot edit `n_recibo` directly in the UI (the field is auto-filled on `resetAllContents()`).

## 3. Encadenamiento — huella criptográfica (SHA-256)

> Art. 12 RD 1007/2023, Anexo de la Orden HAC/1177/2024. Cada registro debe incluir una huella SHA-256 sobre campos clave + la huella del registro anterior, formando una cadena.

**[PARTIAL]**
- In VERIFACTU mode, the SHA-256 hash chain is computed and persisted by the AEAT/IreneSolutions side; the submission response includes the chained `Huella` value in the SOAP XML (e.g. `<sum1:Huella>4486F3131E8AAFA0559F7CB23A1605EC177A15B61F8C595ED2910599CEAC7FCB</sum1:Huella>`).
- La Ideal stores the **CSV** (AEAT's unique identifier) per row in `verifactu_csv`. The CSV is sufficient to locate the AEAT-side record but is not the chained hash.
- **Gap**: the `Huella` field from the response is not persisted locally. Hacienda-side audit can still verify via CSV, but local tamper-detection requires holding the chained hash too.

## 4. Conservación de registros

> Art. 8.2.c RD 1007/2023, Ley 58/2003 LGT. Records must be conserved during the tax prescription period — generally 4 years, longer for some cases.

**[PARTIAL]**
- `ingresos` rows are never deleted by the application.
- DB lives at a hardcoded path on a single machine — no enforced backup, no archival, no retention policy.
- **Gap**: no automated backup or archive procedure. The regulation requires a "procedimiento de descarga, volcado y archivo seguro" (Art. 8.2.c). A nightly SQLite backup + 4-year retention guarantee would close this.

## 5. Trazabilidad

> Art. 8.1 RD 1007/2023. Each invoicing action must be traceable: who, when, from which terminal.

**[PARTIAL]**
- `verifactu_timestamp` records the AEAT submission time per row. ✓
- The app is single-user (no login system), so "who" is implicitly the single operator. The machine identity is also fixed (hardcoded DB path).
- **Gap**: with several operators sharing one machine, the law expects per-user identification. For a one-shop / one-operator deployment this is arguably satisfied by context; for a multi-operator setup it is not. The "declaración responsable" can address single-user scope explicitly.

## 6. Registro de eventos del sistema

> Art. 8.3 RD 1007/2023. **In VERIFACTU mode**, the event log is *not* mandatory (AEAT keeps its own log via the submission stream). **In NO-VERIFACTU mode** it is. Since La Ideal is VERIFACTU, this is informational.

**[COVERED]** (for VERIFACTU mode)
- `AppLogger` captures `qDebug`/`qWarning`/`qCritical` to `~/.laideal.log` with rotation (5 MB), thread-safe via QMutex. Sufficient for technical troubleshooting.
- Note: if La Ideal ever switches to NO-VERIFACTU mode, this becomes insufficient — a structured event log with start/stop, errors, config changes, etc. would be required.

## 7. Exportación y acceso de Hacienda

> Art. 14.1 RD 1007/2023. The Administration can require "acceso completo e inmediato" in legible format. Orden HAC/1177/2024 fixes XML (UTF-8) as the export format with the AEAT schema.

**[MISSING]**
- The Irene Solutions Kivu gateway returns the AEAT-style XML in the `Return.Xml` field of every `/Create` response (already visible in the logs), but La Ideal does not persist that XML and has no export action.
- **Gap**: add an action under Herramientas → Exportar registros AEAT (XML) that dumps `ingresos` rows in the schema-compliant XML format on demand. Two implementation options: (a) persist `Return.Xml` per row and concatenate on export; (b) re-query AEAT for the period. Option (a) is simpler.

## 8. Declaración responsable del fabricante

> Art. 13 RD 1007/2023. The producer must include "una declaración responsable" by which they certify compliance with the regulation. It must be "por escrito y de modo visible en el propio sistema informático en cada una de sus versiones" and include the producer's identifying and location data.

**[MISSING]**
- There is no declaración responsable shown anywhere in the application.
- **Gap**: add an "Acerca de" / "About" dialog (`MainWindow → Ayuda → Acerca de Verifactu`) that displays the declaración responsable — producer NIF + name + address, software name + version, statement of compliance, signature/date. The text is fixed; only producer identifying data and version vary. The AEAT publishes example templates.

## 9. QR y texto obligatorio en el ticket

> Disposición Final Primera RD 1007/2023, Orden HAC/1177/2024. The QR encodes a URL to `TIKE-CONT/ValidarQR` with params `nif`, `numserie`, `fecha`, `importe`. The receipt must also display the **mandatory text** "VERI\*FACTU" or "Factura verificable en la sede electrónica de la AEAT" next to the QR.

**[PARTIAL]**
- QR is generated by Irene Solutions from the response (`result.qrCode`) and embedded by `Imprimir::createTicketExcel()` at 140×140 px. URL points to the correct AEAT endpoint per environment (`TESTING` vs `PRODUCTION`, see `VerifactuConfig::getValidationUrl()`). ✓
- **Gap**: no accompanying mandatory text. The QR sits alone on the receipt. Add a line under (or beside) the QR: `Factura verificable en la sede electrónica de la AEAT` (or the shorter `VERI*FACTU`) — only when `verifactu_estado = 'ENVIADA'`.

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
| 1 | Integrity / inalterabilidad | PARTIAL — no rectificativa UI |
| 2 | Numeración correlativa | COVERED |
| 3 | Hash chain (SHA-256) | PARTIAL — `Huella` not stored locally |
| 4 | Retention (4 años) | PARTIAL — no enforced backup/archive |
| 5 | Trazabilidad | PARTIAL — single-user assumed |
| 6 | Event log | COVERED (VERIFACTU mode) |
| 7 | XML export for Hacienda | MISSING |
| 8 | Declaración responsable | MISSING |
| 9 | QR + mandatory text | PARTIAL — text missing next to QR |
| 10 | No doble-uso software | COVERED |

**Blocking issues filed** (one per non-covered gap) — see `docs/progress_tracker.md`.
