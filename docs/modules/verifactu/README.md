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
| `rawResponse` | `QString` | Full server JSON response (for logging/debugging) |
| `isSuccess()` | `bool` | `status == SUCCESS` |
| `isError()` | `bool` | `ERROR` / `NETWORK_ERROR` / `INVALID_CONFIG` |

### `VerifactuEstado` (DB-persisted state)

Defined in `verifactumanager.h` alongside `verifactuEstadoToString()` / `verifactuEstadoFromString()`:

| Enum | DB string | Meaning |
|------|-----------|---------|
| `NotSubmitted` | `"PENDIENTE"` | Verifactu not configured at save time, or unpaid ticket awaiting submission at pickup. `verifactuEstadoFromString()` also maps NULL/empty (legacy pre-Verifactu rows) here |
| `Enviada` | `"ENVIADA"` | Submitted successfully to AEAT |
| `Anulada` | `"ANULADA"` | Cancelled via `CancelInvoiceDialog` |
| `Rectificada` | `"RECTIFICADA"` | Superseded by a substitution (`S`) rectificativa from `RectifyInvoiceDialog`. Excluded from `totalPriceBetweenDates()` so the new rectificativa row carries the corrected total without double-counting |
| `Error` | `"ERROR"` | Submission or cancellation failed |

Never hardcode the string values — always go through the helpers.

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

> **Internal note**: `VerifactuConfig` also caches a copy to `<AppDataLocation>/verifactu_config.ini` via `QSettings` on every `save()`. This is a legacy artifact — the JSON is authoritative; the INI is never read back because `loadEmitterConfiguration()` always overwrites from AppSettings on startup.

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
| `verifactu_xml` | Raw AEAT-style XML from `Return.Xml` of the `/Create` reply; empty if not submitted or pre-fix. Source for the "Exportar registros AEAT (XML)" action (Art. 14.1 RD 1007/2023). |
| `verifactu_hash` | 64-char hex SHA-256 chained hash extracted from `<sum1:Huella>` in `verifactu_xml`; empty if not submitted or pre-fix. Local tamper-detection (Art. 12 RD 1007/2023). AEAT term: "Huella". |
| `verifactu_rectifies_n_recibo` | On a rectificativa row, points back to the `n_recibo` of the original ticket being corrected; empty on non-rectifying rows |
| `verifactu_rectification_type` | `"S"` (sustitución) or `"I"` (diferencias) on a rectificativa row; empty on non-rectifying rows |

`Contabilidad::totalPriceBetweenDates()` excludes both `verifactu_estado = 'ANULADA'` and `verifactu_estado = 'RECTIFICADA'` rows from quarterly income — cancelled invoices and rows superseded by a substitution rectificativa must not contribute to taxable income (the rectifying row carries the corrected total). All other estados (including `PENDIENTE` and legacy NULL/empty) are included.

---

## Integration points

| Location | Behaviour |
|----------|-----------|
| `MainWindow::on_bb_save_reset_clicked()` | Saves rows with `estado = PENDIENTE`, prints receipts (without QR — AEAT is in flight), fires `verifactuSubmitInvoice()` (paid only), resets form. Status bar shows progress. Async handler `onVerifactuRequestFinished()` UPDATEs the row(s) with CSV when AEAT replies. |
| `RecogPrendas::updateDb(PAY_YES)` | When a ticket is paid late at pickup: re-queries `verifactu_estado` from DB AND checks `hasPendingSubmit(ticketNum)` (in-memory dedup — async submit hasn't updated DB yet, so DB-only check would double-fire from the pay-all loop). If both clear, calls `retryVerifactuSubmit()`. |
| `RecogPrendas::on_pb_verifactu_clicked()` | Opens a dialog showing estado / CSV / timestamp / error / clickable AEAT validation URL. If `estado == ERROR` and configured, also shows "Reintentar envío a AEAT" → calls `retryVerifactuSubmit()` (async, status bar). |
| `CancelInvoiceDialog` (Herramientas → Anular factura Verifactu…) | Async cancel — dialog stays open with "Enviando anulación..." label until AEAT replies. `m_pendingCancelId` guards re-entry. |
| `RectifyInvoiceDialog` (Herramientas → Rectificar factura Verifactu…) | Async R1-R5 rectificativa — operator picks tipo (R1-R5), modo (S/I), date and corrected total or delta. Submits via `submitRectificationAsync()`; on success inserts a new `ingresos` row with the next available `n_recibo` linked back via `verifactu_rectifies_n_recibo`, and for substitution mode marks the original rows `verifactu_estado = RECTIFICADA`. `m_pendingRectifyId` guards re-entry. Art. 8.2.a RD 1007/2023. |
| `MainWindow::on_actionExportar_registros_aeat_triggered()` (Herramientas → Exportar registros AEAT (XML)...) | Prompts for a date range and output path, then writes a single envelope file `<RegistrosFacturacionLaIdeal>` with one `<Registro>` per qualifying ticket (non-empty `verifactu_xml` AND fecha_recepcion in range; ANY `verifactu_estado` since ENVIADA/ANULADA/RECTIFICADA rows were all sent to AEAT). Each row's stored payload is inlined with the `<?xml ?>` declaration stripped so the outer document stays well-formed. Required by Art. 14.1 RD 1007/2023 ("acceso completo e inmediato"). |
| `Imprimir::createTicketExcel()` | Embeds the QR pixmap at the bottom of the receipt (140×140). `resolveQrCode()` returns the in-memory pixmap if present, otherwise fires `generateQRAsync()` and waits up to 5s in a local `QEventLoop`. Times out gracefully — prints without QR + log warning. Save-time prints never hit this path because the DB CSV is empty. |

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

`VerifactuManager::processResponse()` extracts the following from `Return`:

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
| `NETWORK_ERROR` at save | Warning dialog at save; row written with `estado = ERROR` | Retry from RecogPrendas → Verifactu button → "Reintentar envío a AEAT" |
| AEAT `8002 "CompanyName es obligatorio"` | `result.errorDescription` | Set `verifactu.name` in Settings — required by AEAT |
| AEAT `8003 "InvoiceID duplicado"` | `result.errorDescription` | Each `n_recibo` must be unique per emitter per day — usually a sign of accidental re-submission |
| AEAT `3002` on cancel (TESTING only) | `CancelInvoiceDialog` | TESTING purges records — submit a fresh invoice in the same session first |

---

## Security

- ServiceKey lives in plaintext JSON (`~/.laideal_settings.json`). Acceptable for single-user desktop deployment; consider encryption for multi-user scenarios. Tracked in [`progress_tracker.md`](../../progress_tracker.md) under non-blocking issues.
- Never mix real data into TESTING.
- Always validate end-to-end in TESTING before switching `verifactu.production` to `true`.
