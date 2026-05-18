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

These columns must be added to `ingresos` (or `facturas`) before persisting Verifactu results:

```sql
ALTER TABLE facturas ADD COLUMN verifactu_csv VARCHAR(50);
ALTER TABLE facturas ADD COLUMN verifactu_timestamp DATETIME;
ALTER TABLE facturas ADD COLUMN verifactu_estado VARCHAR(20);   -- ENVIADA, ERROR, ANULADA
ALTER TABLE facturas ADD COLUMN verifactu_error VARCHAR(255);
ALTER TABLE facturas ADD COLUMN verifactu_url_qr VARCHAR(500);
```

Optional retry-tracking table:

```sql
CREATE TABLE facturas_reintentos (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    numero_factura VARCHAR(50),
    fecha_intento DATETIME,
    numero_intentos INTEGER,
    ultimo_error VARCHAR(255)
);
```

---

## Security

- **ServiceKey**: store in INI file only, not in source control. Rotate periodically.
- **NIF**: confidential — same rule as ServiceKey.
- **Environments**: never mix real data into TESTING. Always validate in TESTING before switching to PRODUCTION.
- **Future**: consider `QSettings` encryption for the INI file.

---

## Roadmap

- [ ] Persist CSV and QR URL to DB after successful submission (blocking — see progress tracker)
- [ ] Configuration UI panel in Qt
- [ ] Retry queue for failed submissions
- [ ] Local invoice cache for offline scenarios
- [ ] Support multiple companies

---

## FAQ

**No ServiceKey?** — Module works in TESTING mode without one. Required for PRODUCTION.

**Network error?** — `VerifactuResult.status` is `NETWORK_ERROR`. Save the invoice locally first, then retry.

**How to prevent invoice loss?** — Persist the invoice to DB before submitting; update the DB row after a successful response.
