# Verifactu Module (`src/verifactu/`)

AEAT mandatory digital invoicing integration. Submits invoices to AEAT in real time via the Irene Solutions REST gateway, captures the CSV security code and QR image, and persists them with the ticket. Required for Spanish businesses from 2026.

**Status (v8.0)**: production-ready. TESTING environment validated end-to-end (submit, cancel, QR on printed receipt, retry on failure). Production credentials pending: requires meeting with Irene Solutions for the live ServiceKey.

For the full AEAT REST API field reference see [`rest_api.md`](./rest_api.md).

---

## Source files

| File | Purpose |
|------|---------|
| `verifactuintegration.h/cpp` | Public facade — the only class `MainWindow` and friends call directly |
| `verifactumanager.h/cpp` | HTTP REST layer over `QNetworkAccessManager`. Also defines `VerifactuEstado` enum + string helpers |
| `verifacturesponse.h/cpp` | Pure `parseVerifactuResponse()` (JSON reply -> `VerifactuResult`) + `decodeVerifactuImageBase64()`, extracted from `VerifactuManager` so the parsing is unit-testable without a network |
| `verifactuconfig.h/cpp` | Internal config holder (NIF, name, ServiceKey, environment, endpoint URLs) |
| `verifactuinvoice.h/cpp` | JSON-serialisable `VerifactuInvoice` and `VerifactuTaxItem` models |

## Architecture

```
MainWindow / RecogPrendas / CancelInvoiceDialog / RectifyInvoiceDialog / Imprimir
        │
        ▼
VerifactuIntegration (facade)
        │
        ▼
VerifactuManager (HTTP via QNetworkAccessManager)
        │   ┌─────────────────────────┐
        ├──▶│ VerifactuConfig         │  (NIF, name, ServiceKey, env)
        └──▶│ VerifactuInvoice + Item │  (JSON payload)
            └─────────────────────────┘
```

All consumers go through `VerifactuIntegration` (or in the case of `SettingsDialog::testConnection`, a transient `VerifactuManager` with dialog-only values). The API is fully async — calls return a request ID immediately and the result arrives via `requestFinished`.

---

## Public API (`VerifactuIntegration`)

```cpp
m_verifactuIntegration = new VerifactuIntegration(this);
m_verifactuIntegration->initialize();    // non-fatal - only warns if not configured
m_verifactuIntegration->isConfigured();  // true once NIF + name + ServiceKey are set

connect(m_verifactuIntegration, &VerifactuIntegration::requestFinished,
        this, &MyClass::onVerifactuRequestFinished);

// F2 simplified invoice - laundry tickets (no buyer NIF). Returns a request ID.
QString reqId = m_verifactuIntegration->submitSimplifiedInvoiceAsync(
    ticketNumber, ticketDate, taxBase, ivaRate, "Servicios de lavanderia");

// Cancel a previously submitted invoice
QString reqId = m_verifactuIntegration->cancelInvoiceAsync(invoiceNumber, invoiceDate);

// Submit a rectificativa (R1-R5) of a previously submitted invoice.
// For BY_DIFFERENCES (I): newTaxBase/Amount carry the delta (signed); origTaxBase/Amount are ignored.
// For BY_SUBSTITUTION (S): newTaxBase/Amount carry the corrected total;
//                          origTaxBase/Amount carry the values being replaced
//                          (sent as RectificationTaxBase / RectificationTaxAmount).
QString reqId = m_verifactuIntegration->submitRectificationAsync(
    newInvoiceNumber, invoiceDate,
    VerifactuInvoice::RECTIFICATION_R1,
    VerifactuInvoice::BY_DIFFERENCES,
    newTaxBase, newTaxAmount,
    origTaxBase, origTaxAmount,
    ivaRate, "Rectificativa de ticket ...");

// Re-fetch QR for an already-submitted invoice (used by Imprimir reprint path)
QString reqId = m_verifactuIntegration->generateQRAsync(
    invoiceNumber, invoiceDate, taxBase, ivaRate, description);

// Result arrives later via the signal:
void MyClass::onVerifactuRequestFinished(const QString &reqId, const VerifactuResult &result) {
    if (reqId != m_pendingId) return;   // not ours
    // ... handle result ...
}
```

An empty returned `QString` means the call was rejected synchronously (Verifactu not configured) - no signal fires for it. Otherwise the signal is guaranteed to fire exactly once, even for validation errors and invalid-config cases (delivered via `Qt::QueuedConnection`).

### `VerifactuResult`

| Field | Type | Notes |
|-------|------|-------|
| `status` | `SUCCESS` / `PENDING` / `ERROR` / `NETWORK_ERROR` / `INVALID_CONFIG` | API call outcome |
| `csv` | `QString` | AEAT security code (e.g. `A-9VARYQTZTARVU2`) — persist to DB |
| `errorCode` | `QString` | AEAT error code on failure |
| `errorDescription` | `QString` | Human-readable error message |
| `validationUrl` | `QString` | AEAT portal URL with invoice params pre-filled |
| `qrCode` | `QPixmap` | BMP QR image decoded from base64 |
| `isSuccess()` | `bool` | `status == SUCCESS` |

### `VerifactuEstado` (DB-persisted state)

Defined in `verifactumanager.h` alongside `verifactuEstadoToString()` / `verifactuEstadoFromString()`:

| Enum | DB string | Meaning |
|------|-----------|---------|
| `Unpaid` | `"SIN COBRAR"` | Unpaid row: there is no invoice to submit yet. Written by `saveTicket` / `AddGarment` when `pagado != "SI"`; becomes `PENDIENTE` only once the garment is paid and a submit is actually due |
| `NotSubmitted` | `"PENDIENTE"` | Paid and due at AEAT: submitted and awaiting a reply, or not sent yet (Verifactu not configured). `verifactuEstadoFromString()` also maps NULL/empty (legacy pre-Verifactu rows) here |
| `Enviada` | `"ENVIADA"` | Submitted successfully to AEAT |
| `Anulada` | `"ANULADA"` | Cancelled via `CancelInvoiceDialog` (only the paid rows of that payment event; the ticket's unpaid garments stay `SIN COBRAR` and chargeable). Also set by Anular prendas on a locally voided garment. Gets `fecha_anulacion` |
| `Rectificada` | `"RECTIFICADA"` | Superseded by a substitution (`S`) rectificativa from `RectifyInvoiceDialog` (only the paid rows; unpaid garments stay chargeable). Gets `fecha_anulacion` = the rectificativa's date: it stays income in its payment period and is subtracted in that date's period (see `docs/modules/contabilidad.md`). Pre-10.12 rows without the date are simply excluded from `kIngresosIncomeWhere` |
| `Error` | `"ERROR"` | Submission or cancellation failed |

Never hardcode the string values — always go through the helpers, and map an AEAT reply with `verifactuEstadoForResult(result.status)` rather than testing `isSuccess()` by hand: only a definitive AEAT rejection (`ERROR`) may become `Error`. A timeout or transport failure leaves the outcome **unknown** — AEAT may already hold the invoice — so it becomes `PENDIENTE` and is routed to the startup recovery dialog. Recording it as `Error` instead offers the operator a "Reintentar" that can only ever answer "already exists".

### Bounded waits vs the transport timeout

`VerifactuManager` sets a **10 s** transfer timeout, while the UI waits **3 s** (MainWindow save) and **5 s** (PayDialog). The UI therefore gives up first *by design* — a slow AEAT must not freeze the counter — which means a request is often still in flight when the ticket has already printed. Two consequences the code must honour:

- the printed document degrades to a **recibo with IMPORTE PAGADO and no QR** (a recibo is a claim ticket, never a tax document, so it must not carry a QR);
- the in-flight request must be **adopted by an object that outlives the print** (`RecogPrendas` for PayDialog; MainWindow already owns its own `m_pendingSubmits`), so a late reply still patches the row. On a late success both paths tell the operator the factura with QR can now be printed.

Do not "fix" this by raising the waits to 10 s without weighing the counter freeze — the adoption path already prevents data loss.

### Reconciling with AEAT (`GetFilteredList`)

When AEAT has registered an invoice whose reply we lost, every retry answers "ya existe" and the row is stuck at `ERROR`. `<base>/GetFilteredList` resolves it by asking AEAT what it actually holds.

Request (per the vendor's `Net/Rest/List/FilterSet.cs`; `ServiceKey` goes in the body like every other call):

```json
{"ServiceKey":"…","Filters":[{"FieldName":"InvoiceID","Operator":"=","Value":"30877"}],"Count":10,"Offset":-1}
```

**The response schema is NOT published** — the vendor's own client parses it untyped. `parseVerifactuQueryResponse()` therefore probes several envelopes (`Return` / `Records` / `List` / `Items` / bare array) and field spellings, normalises dates to `dd-MM-yyyy`, and keeps the whole payload in `raw`. It always logs the payload: **the first real reply is what lets these candidates be narrowed to the truth**, so if you have one, pin the parser and delete the guesses.

**The service stores every submission *attempt*, not one record per invoice.** A ticket submitted once and retried four times comes back as **five** records under the same `InvoiceID`: four duplicate-rejections (`ErrorCode 9999`, `CSV: null`) plus the original acceptance — and the accepted one is **not** necessarily first. `parseVerifactuQueryResponse()` therefore scans for the accepted record (a CSV, no `ErrorCode`, not `IsRejected`) rather than taking `Items[0]`, and reports `recordCount` so the dialog can say how many attempts were stored. Taking the first record read a failure and hid the CSV on an invoice AEAT demonstrably held (ticket 31121).

Three rules keep an unconfirmed schema from causing a false regulatory claim:

1. **Ignorance is not absence.** A failed query or an unreadable body yields `parsed=false`, never "AEAT does not have it". Only a well-formed empty result means absent.
2. **Identity must be positively proven.** `verifactuRemoteMatches()` requires InvoiceID + date + amount to agree to the cent; a missing date or a zero amount is missing evidence, not agreement.
3. **The operator confirms.** Nothing is written automatically. The dialog shows AEAT's record beside the local one with the raw JSON one click away, and enables the write only when the match holds *and* a CSV came back.

The write is `sql_lite::reconcileVerifactuFromAeat()`, which refuses an empty CSV and never touches a row already `ENVIADA` / `ANULADA` / `RECTIFICADA`. Entry points: a **Consultar en AEAT** button in the Verifactu detail dialog, and an automatic query when `verifactuErrorIsDuplicate()` recognises a duplicate rejection.

`Unpaid` and `NotSubmitted` are distinguished **only** by the startup recovery dialog, which needs to tell "nothing to send" from "sent, reply lost". Every other gate must treat them alike via `verifactuEstadoIsUnsubmitted()` (true for `Unpaid`, `NotSubmitted` and legacy blank): paying a garment does not rewrite `verifactu_estado`, so a row is still `SIN COBRAR` at the moment `RecogPrendas` decides whether to submit it — testing `== NotSubmitted` there would silently stop paid garments from reaching AEAT. The two callers are `sql_lite::garmentIsLocallyVoidable` and the `RecogPrendas` `PAY_YES` submit trigger.

`migrateDatabase()` carries a one-time idempotent backfill re-labelling unpaid `'PENDIENTE'` rows as `'SIN COBRAR'`. It is scoped to the literal `'PENDIENTE'` and leaves NULL/`''` alone on purpose: those are legacy payments (before Verifactu, or made while it was off), which `Imprimir`'s event-list query and `CancelInvoiceDialog` leave out via `verifactu_estado != ''`.

10.12 adds a second repair: unpaid garments that a pre-10.12 AEAT cancellation wrongly marked `ANULADA` (they share seq 0 with the paid rows) go back to `'SIN COBRAR'`, so they can be charged as their own invoice. Unpaid rows marked `RECTIFICADA` are **not** repaired, because an older substitution may have been submitted with their amounts included. Each such ticket is logged as a warning for manual review instead.

11.0 adds a third: before 11.0, Separar prendas inserted the split-off garments of a paid row with blank `verifactu_*` columns at seq 0, outside the invoice AEAT registered. A paid row with a blank estado at seq 0 is copied the `verifactu_*` columns of the one `ENVIADA` invoice on its ticket with the same `fecha_pago`, `prenda` and `servicio`. Amounts are never changed, and relinking an `ENVIADA` invoice moves no Contabilidad figure. A match with a cancelled or rectified invoice, or with two invoices, is only logged. Since 11.0 a split (`sql_lite::splitGarmentRow`) copies the whole row, so it stays in its invoice.

---

## Configuration

Source of truth: `~/.laideal_settings.json`, managed by `AppSettings`. Edit via **Archivo → Configuración… → Verifactu tab**.

| JSON key | Content |
|----------|---------|
| `verifactu.nif` | Emitter NIF |
| `verifactu.name` | Emitter name (`CompanyName` sent to AEAT) |
| `verifactu.serviceKey` | API ServiceKey — obtain at https://facturae.irenesolutions.com/verifactu/go |
| `verifactu.production` | `false` = TESTING, `true` = PRODUCTION |

`VerifactuIntegration::loadEmitterConfiguration()` reads these on `initialize()` and pushes them into `VerifactuConfig`. `initialize()` is non-fatal — it only warns if NIF or name is empty, and the rest of the app remains usable (tickets save without an AEAT submission, with `verifactu_estado = "PENDIENTE"`).

---

## DB persistence (`ingresos` table)

Nine columns added to `ingresos` by `migrateDatabase()` in `sql_lite.cpp` (idempotent `ALTER TABLE ADD COLUMN`):

| Column | Content |
|--------|---------|
| `verifactu_csv` | AEAT security code; empty if not submitted |
| `verifactu_timestamp` | ISO-8601 submission timestamp; empty if not submitted |
| `verifactu_estado` | See `VerifactuEstado` table above |
| `verifactu_error` | Error description when `estado = ERROR`; empty otherwise |
| `verifactu_url_qr` | AEAT `ValidationUrl` (for QR/portal verification); empty if not submitted |
| `verifactu_xml` | Raw AEAT-style XML from `Return.Xml` of the `/Create` reply; empty if not submitted, pre-fix, or recovered with "Consultar en AEAT" (which adopts only the CSV). Source for the "Exportar registros AEAT (XML)" action (Art. 14.1 RD 1007/2023); an invoice with a CSV but no payload is exported with `sinPayload="1"`. |
| `verifactu_cancel_xml` | AEAT's cancellation record, `Return.Xml` of the `/Cancel` reply, stored on the cancelled event's paid rows; empty for cancellations made before October 2026. Exported as `<Anulacion>`. |
| `verifactu_hash` | 64-char hex SHA-256 chained hash extracted from `<sum1:Huella>` in `verifactu_xml`; empty if not submitted or pre-fix. Local tamper-detection (Art. 12 RD 1007/2023). AEAT term: "Huella". |
| `verifactu_rectifies_n_recibo` | On a rectificativa row, points back to the `n_recibo` of the original ticket being corrected; empty on non-rectifying rows |
| `verifactu_rectification_type` | `"S"` (sustitución) or `"I"` (diferencias) on a rectificativa row; empty on non-rectifying rows |

The Contabilidad income predicate (`kIngresosIncomeWhere` in `sql_lite.cpp`) excludes both `verifactu_estado = 'ANULADA'` and `verifactu_estado = 'RECTIFICADA'` rows from quarterly income — cancelled invoices and rows superseded by a substitution rectificativa must not contribute to taxable income (the rectifying row carries the corrected total). All other estados (including `PENDIENTE` and legacy NULL/empty) are included. Exception: a row cancelled or rectified from 10.12 on carries `fecha_anulacion`; it stays income in its payment period and is subtracted in the period of `fecha_anulacion` (see `docs/modules/contabilidad.md` → Cancellations are counted where they happen).

---

## Integration points

| Location | Behaviour |
|----------|-----------|
| `MainWindow::on_pb_save_clicked()` (Guardar ticket) | Saves the ticket's rows in one transaction (`sql_lite::insertGarmentRows`: all or none) with `estado = PENDIENTE` when paid / `SIN COBRAR` when not; if they cannot be stored nothing is kept, sent or printed. A paid ticket fires `verifactuSubmitInvoice()` with the stored total and waits up to 3 s for the reply, printing the factura with QR if it came (else the receipt without QR); resets the form and summarises the save in the result panel. Async handler `onVerifactuRequestFinished()` UPDATEs the row(s) with CSV when AEAT replies. |
| `RecogPrendas::on_pb_verifactu_clicked()` | Opens a dialog showing estado / CSV / timestamp / error / clickable AEAT validation URL. If `estado == ERROR` and configured, also shows "Reintentar envío a AEAT" → calls `retryVerifactuSubmit()` (async, status bar). |
| `AddGarment` → `paidGarmentSaved` → `MainWindow::verifactuSubmitInvoice()` | A garment added as paid (Herramientas → Añadir nuevas prendas) is the ticket's first invoice (tickets with a paid garment are refused, re-checked at save against the number actually saved), so it is submitted at once as seq 0, `InvoiceID` = `n_recibo`, with its payment date. No bounded wait: the reply is applied by `onVerifactuRequestFinished`; without one the row stays `PENDIENTE` for the startup recovery. |
| `PayDialog::onCobrarClicked()` (partial payment) | Submits the selected garments as `InvoiceID = "<n_recibo>-<seq>"` (`nextVerifactuInvoiceSeq`) with a 5 s bounded wait. On reply: `SUCCESS` → `ENVIADA` + CSV/QR; AEAT `ERROR` → `Error`. On **timeout / transport failure** (`NETWORK_ERROR`/`PENDING`) the outcome is unknown, so `markPendingVerifactu(seq)` records the rows `PENDIENTE` (not `Error`), keeping the `<n_recibo>-<seq>` InvoiceID. Both write-backs are scoped `AND pagado = 'SI'`: a ticket's **first** payment event gets seq 0 (`nextVerifactuInvoiceSeq` counts paid rows), and the unpaid remainder carries seq 0 too, so seq alone would stamp it with a result for an invoice that never covered it. The recibo fallback prints **IMPORTE PAGADO** (the rows are already `pagado='SI'`), and before closing, the dialog emits `submitAdopted(reqId, ticketNum, seq)` so `RecogPrendas` — which outlives it — applies a reply that arrives after the wait expired. Verifactu-disabled → rows paid, `verifactu_*` left empty. An unmeasured m2 garment (`garmentUnmeasured`) cannot be charged: Cobrar refuses it in the result panel. After AEAT's reply each garment is stored as paid only if it is still unlocked and unpaid (one row changed); one that was locked or paid meanwhile is logged (`qCritical`) and counted (`PayDialog::unstoredGarments`), and Recogida shows it in red instead of "cobrado" so the ticket is reviewed. The dialog uses the shared UiKit style: ticket header panel, the garments table, a bold total, **Cancelar** / **Cobrar** and a result panel where "nothing selected", a closed quarter (`quarterIsClosed`, the whole quarter) and the "Enviando…" progress appear instead of message boxes; only "No se pudo enviar a AEAT, se registra localmente" is still a message box, because the dialog closes right after. |
| `PendingSubmitsDialog` (startup recovery) | Lists submission events left `PENDIENTE`/NULL/empty (one per `(n_recibo, verifactu_invoice_seq)` via `sql_lite::pendingVerifactuEvents`, gated by `verifactu.pending_recovery_enabled` + floor date + **paid** (`pagado='SI'` with a `fecha_pago`) — an unpaid ticket was never submitted, so it is not pending reconciliation) and offers Reintentar / Error / Posponer. Seq-aware: each row shows its `<n>-<seq>` InvoiceID and Reintentar re-submits that event's own `SUM(importe)` under `verifactuInvoiceId(n_recibo, seq)`, so partial-pay events (`seq>0`) are recovered too — not only save-time / full-ticket (`seq=0`) submissions. Mark-Error scopes its UPDATE by `seq`. A slow-but-already-registered AEAT submission is caught by the duplicate-InvoiceID rejection. UiKit style: one explanation panel, the table, and a result panel reporting each action (re-sent, marked ERROR, postponed, no payment date, update failed) instead of message boxes. |
| `CancelInvoiceDialog` (Herramientas → Anular factura Verifactu…) | Async cancel — one per payment event, sent with the event's InvoiceID and its **payment date** (the earliest `fecha_pago` of its paid rows, from `sql_lite::submittedInvoiceEvents` - the same rule as retry and query; shown in the dialog's "Fecha factura" column). An event without a readable payment date is refused locally. If AEAT rejects the cancellation of a seq-0 invoice whose reception date differs, it is retried once with the reception date: before 10.9 a failed submission was re-sent under it. The dialog stays open with "Enviando anulación..." label until AEAT replies. `m_pendingCancelId` guards re-entry. Refused while today's quarter is closed (`sql_lite::quarterIsClosed`), since the cancellation is accounted today; the date stamped is the date checked at click time. A failed local write after AEAT accepted is reported in red instead of a success message. Lists one line per payment event over **paid** rows only (unpaid garments share seq 0 but are not part of the invoice). After a failed local write the event still shows ANULADA so it cannot be cancelled twice; a quarter closed while the request was in flight moves the stamped date to today. |
| `RectifyInvoiceDialog` (Herramientas → Rectificar factura Verifactu…) | Async R1-R5 rectificativa — operator picks tipo (R1-R5), modo (S/I), date and corrected total or delta. Submits via `submitRectificationAsync()`; on success inserts a new `ingresos` row with the next available `n_recibo` linked back via `verifactu_rectifies_n_recibo`, and for substitution mode marks the original rows `verifactu_estado = RECTIFICADA`. `m_pendingRectifyId` guards re-entry. Art. 8.2.a RD 1007/2023. Refuses a date in a closed quarter or before the original payment (`ticketLastPaymentDate`); a failed local RECTIFICADA mark after AEAT accepted stays on screen in red instead of the success message. |
| `AeatExportDialog` (`src/app/aeatexportdialog.cpp`; Herramientas → Exportar registros AEAT (XML)...) | A UiKit-style window: Periodo (Desde / Hasta, default the last three months), Archivo (default `<reports root>/AEAT/aeat_registros_<desde>_<hasta>.xml`, following the dates until one is typed or picked with Elegir…), **Exportar registros**; the count, a link to the file and any warning (empty period, undated invoices, write error) appear in its result panel, no message boxes. It writes a single envelope file `<RegistrosFacturacionLaIdeal fechaDesde="…" fechaHasta="…" generadoEl="…" nif="…" emisor="…">` containing one `<Registro nRecibo="…" invoiceId="…" fechaPago="…" importe="…" csv="…" estado="…">` element per **invoice AEAT holds**, i.e. per paid payment event `(n_recibo, verifactu_invoice_seq)` with a stored payload or a CSV, whatever its current estado (cancelled and rectified invoices included), issued (`fecha_pago`) or cancelled (`fecha_anulacion`) in the range. Optional attributes: `fechaAnulacion` (cancelled / substituted), `rectifica` + `tipoRectificacion` (a rectificativa and the ticket it corrects), `sinPayload="1"` (known only by its CSV, e.g. recovered with "Consultar en AEAT", which stores no payload). A `<Registro>` appears in the period the invoice was issued; a cancellation (`ANULADA`) also appears as `<Anulacion invoiceId fechaPago fechaAnulacion>` in the period it was made, with AEAT's cancellation record (`verifactu_cancel_xml`) inlined, or `sinPayload="1"` for cancellations made before it was stored; one made before 10.12 has no date, so it is written right after its `<Registro>` with `sinFecha="1"`. `fechaExpedicion` is added to either element when the stored payload names another invoice date than the payment (the first `FechaExpedicionFactura` / `FechaExpedicionFacturaAnulada`: older invoices sent under the reception date). A payload that cannot be inlined (not well-formed, a DOCTYPE, an undeclared namespace prefix) is written as escaped text with `payloadComoTexto="1"`, so the file always parses. Invoices AEAT holds without a readable payment date belong to no period; their number is shown in the result message. A failed query shows an error instead of writing an empty file. `invoiceId` is the literal AEAT InvoiceID (`<n>` or `<n>-<seq>`), `importe` the event total. The records come from `sql_lite::aeatExportRecords`; `writeAeatExportXml` (`src/app/aeatexport.cpp`) writes the file, inlining each payload once with its `<?xml ?>` declaration stripped so the outer document stays well-formed. |
| `Imprimir::buildTicket()` | Rasters the QR pixmap into the ESC/POS stream at the bottom of the receipt (`qr.scaled(192).toImage()` → `EscPosBuilder::rasterImage`, `GS v 0` — the exact AEAT pixmap, not re-encoded). `resolveQrCode()` returns the in-memory pixmap if present, otherwise fires `generateQRAsync()` and waits up to 5s in a local `QEventLoop`. Times out gracefully — prints without QR + log warning. Save-time prints never hit this path because the DB CSV is empty. |

---

## Environments

| Environment | When | AEAT validation URL |
|-------------|------|---------------------|
| TESTING | Development / pre-production validation | `https://prewww2.aeat.es/wlpl/TIKE-CONT/ValidarQR` |
| PRODUCTION | Live submission to AEAT | `https://www2.aeat.es/wlpl/TIKE-CONT/ValidarQR` |

REST gateway (both environments, hosted by Irene Solutions): `https://facturae.irenesolutions.com:8050/Kivu/Taxes/Verifactu/Invoices`

Endpoints used:
- `POST /Create` — submit a single invoice
- `POST /Cancel` — cancel an invoice (must include `CompanyName` alongside `InvoiceID` / `InvoiceDate` / `SellerID`)
- `POST /GetQrCode` — fetch QR for an already-submitted invoice (used by Imprimir on the reprint path)

> **TESTING gotcha**: AEAT TESTING periodically purges submitted records. Error `3002 "No existe el registro de facturación"` on a cancel means the original submission has been purged — submit a fresh invoice in the same session before testing the cancel flow.

---

## Response capture (what AEAT returns on `/Create` success)

`parseVerifactuResponse()` (the pure parser in `verifacturesponse.{h,cpp}`, called by `VerifactuManager` and unit-tested in `tests/test_verifactu_response.cpp`) extracts the following from `Return`:

| Field | Captured as | Persisted to DB |
|-------|-------------|-----------------|
| `CSV` | `result.csv` | `verifactu_csv` |
| `QrCode` (base64 BMP) | `result.qrCode` (`QPixmap`) | not stored (regenerated via `/GetQrCode` on reprint) |
| `ValidationUrl` | `result.validationUrl` | `verifactu_url_qr` |
| `ErrorCode` | `result.errorCode` | (only when `estado = ERROR`) |
| `ErrorDescription` | `result.errorDescription` | `verifactu_error` |
| `Xml` | `result.rawXml` | `verifactu_xml` (source for the AEAT export action) |
| `<sum1:Huella>` inside `Xml` | `result.rawHash` (regex-extracted, upper-case hex) | `verifactu_hash` (Art. 12 RD 1007/2023) |

Not captured: `QrCodeUrl` (direct URL to QR on Irene servers — we have the pixmap), `ExternKey` (blockchain id), `StatusResponse` (`ResultCode` used instead), `Response` (raw HTTP response — JSON is parsed and the AEAT XML is the only payload we keep).

---

## Common error scenarios

| Error / situation | Where it surfaces | Resolution |
|-------------------|-------------------|------------|
| `INVALID_CONFIG` at save | Silent (logged only) | Configure NIF / name / ServiceKey in Settings |
| `NETWORK_ERROR` at save | The outcome is unknown, so the row stays `PENDIENTE` (`verifactuEstadoForResult`), never `ERROR` | Retried from the startup recovery dialog (Envíos Verifactu pendientes); a duplicate answer is reconciled with "Consultar en AEAT" |
| AEAT `8002 "CompanyName es obligatorio"` | `result.errorDescription` | Set `verifactu.name` in Settings — required by AEAT |
| AEAT `8003 "InvoiceID duplicado"` | `result.errorDescription` | Each `n_recibo` must be unique per emitter per day — usually a sign of accidental re-submission |
| AEAT `3002` on cancel | `CancelInvoiceDialog` | TESTING purges records — submit a fresh invoice in the same session first. In production it means AEAT holds no invoice with that InvoiceID and date; the dialog already retries an old seq-0 invoice with its reception date. Check what AEAT holds with "Consultar en AEAT" in Recogida |

---

## Security

- ServiceKey is encrypted at rest in `~/.laideal_settings.json` with Windows DPAPI (per-user `CryptProtectData`, `dpapi:v1:` marker); `AppSettings` decrypts transparently on read and legacy plaintext is auto-migrated on load. Note DPAPI binds the ciphertext to the Windows user+machine, so the key must be re-entered after a reinstall or migration to another account.
- Never mix real data into TESTING.
- Always validate end-to-end in TESTING before switching `verifactu.production` to `true`.
