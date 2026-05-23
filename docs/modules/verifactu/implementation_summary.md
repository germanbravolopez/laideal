# Verifactu — Technical Reference

> For step-by-step implementation see `step_by_step_guide.md`. For overview and quick start see `README.md`.

## Class reference

### VerifactuConfig
Persists configuration via `QSettings` (INI file). Key members: `m_serviceKey`, `m_emitterNIF`, `m_environment` (`TESTING` / `PRODUCTION`).

### VerifactuInvoice + VerifactuTaxItem
JSON-serialisable data models. Supported invoice types: F1 (standard), F2 (simplified), F3 (substitution), R1 (corrective). Operations: S1, S2, N1, N2, Exempt.

### VerifactuManager
HTTP REST communication layer. Key methods:
- `submitInvoice()` / `submitInvoices()` — single and batch submission
- `cancelInvoice()` — annul a submitted invoice
- `generateQRCode()` — generate QR image from CSV

### VerifactuIntegration
Public facade (only class `MainWindow` calls directly):
- `initialize()` — validates config; non-fatal if not configured
- `createAndSubmitInvoice()` — build + send in one call
- `cancelInvoice()` / `generateQR()` — delegated operations

---

## Database schema changes

Five `verifactu_*` columns are added to `ingresos` automatically at startup via `migrateDatabase()` in `sql_lite.cpp`:

| Column | Values |
|--------|--------|
| `verifactu_csv` | AEAT security code (e.g. `A-9VARYQTZTARVU2`), empty if not submitted |
| `verifactu_timestamp` | ISO-8601 timestamp of submission, empty if not submitted |
| `verifactu_estado` | `ENVIADA` / `ERROR` / `ANULADA` / `PENDIENTE` (legacy rows may be NULL/empty) |
| `verifactu_error` | Error description when `estado = ERROR`, empty otherwise |
| `verifactu_url_qr` | AEAT `ValidationUrl`, empty if not submitted |

The `ALTER TABLE ADD COLUMN` calls are idempotent — SQLite silently ignores them on subsequent runs.

---

## Security

- **ServiceKey**: store in INI file only, not in source control. Rotate periodically.
- **NIF**: confidential — same rule as ServiceKey.
- **Environments**: never mix real data into TESTING. Always validate in TESTING before switching to PRODUCTION.
- **Future**: consider `QSettings` encryption for the INI file.

---

## Roadmap

- [x] Persist CSV, ValidationUrl, estado and error to DB after submission — done via `migrateDatabase()` + `saveTicket(VerifactuResult)`
- [x] `processResponse()` captures `ValidationUrl` and `QrCode` from the `/Create` response
- [ ] Configuration UI panel in Qt
- [ ] Retry queue for failed submissions
- [ ] Local invoice cache for offline scenarios
- [ ] Support multiple companies

---

## FAQ

**No ServiceKey?** — Module works in TESTING mode without one. Required for PRODUCTION.

**Network error?** — `VerifactuResult.status` is `NETWORK_ERROR`. Save the invoice locally first, then retry.

**How to prevent invoice loss?** — Persist the invoice to DB before submitting; update the DB row after a successful response.
