# Verifactu Module (`src/verifactu/`)

AEAT mandatory digital invoicing integration (required for Spanish businesses from 2026). Submits invoices to AEAT in real time, receives a CSV security code, and generates a client QR code.

**Status**: TESTING confirmed (CSV received from AEAT). Pending: DB persistence, QR on receipt, production credentials.

## Source files

| File | Purpose |
|------|---------|
| `verifactuintegration.h/cpp` | Facade — the only class `MainWindow` interacts with |
| `verifactumanager.h/cpp` | HTTP REST via `QNetworkAccessManager` |
| `verifactuconfig.h/cpp` | `QSettings` INI config: serviceKey, NIF, environment |
| `verifactuinvoice.h/cpp` | JSON-serialisable invoice and tax-line models |

## Architecture

```
MainWindow → VerifactuIntegration (facade)
               └── VerifactuManager (HTTP)
                     ├── VerifactuConfig (QSettings)
                     └── VerifactuInvoice + VerifactuTaxItem
```

## Key interface

```cpp
// Initialise in MainWindow constructor
m_verifactuIntegration = new VerifactuIntegration(this);
m_verifactuIntegration->initialize();  // warns if not configured — non-fatal

// Submit a simplified (F2) invoice — used for laundry retail tickets
VerifactuResult result = m_verifactuIntegration->submitSimplifiedInvoice(
    ticketNumber, date, taxBase, 21.0, "Servicios de lavanderia");
// result.status: SUCCESS | ERROR | NETWORK_ERROR | INVALID_CONFIG
// result.csv:    "A-9VARYQTZTARVU2"  (AEAT security code — persist to DB)
// result.qrCode: QPixmap             (BMP QR image — add to receipt)
// result.validationUrl               (AEAT portal link — show in RecogPrendas)

// Submit a full (F1) invoice — requires buyer NIF
VerifactuResult result = m_verifactuIntegration->createAndSubmitInvoice(
    invoiceNumber, date, buyerNIF, buyerName, taxBase, 21.0, description);
```

## Configuration files (not in source control)

| File | Content | Example |
|------|---------|---------|
| `~/.laideal_cfg` | Emitter NIF and name | `nif=12345678A` / `name=La Ideal SL` |
| `~/.verifactu_key` | API service key (one line) | `abc123...` |

`initialize()` returns `false` and shows a warning if either file is missing or incomplete.

## Environments

| Environment | Description | QR validation URL |
|-------------|-------------|-------------------|
| TESTING | Safe for dev — no real AEAT submission | `https://prewww2.aeat.es/wlpl/TIKE-CONT/ValidarQR` |
| PRODUCTION | Live — submits to AEAT | `https://www2.aeat.es/wlpl/TIKE-CONT/ValidarQR` |

REST API endpoint (both environments): `https://facturae.irenesolutions.com:8050/Kivu/Taxes/Verifactu/Invoices`

## What the API returns on success

The `submitInvoice` response includes all of these in `Return`:

| Field | Description | Captured |
|-------|-------------|---------|
| `CSV` | AEAT security code — must be shown to client and stored | Yes (`result.csv`) |
| `QrCode` | Base64 BMP image — print on receipt and show in RecogPrendas | Yes (`result.qrCode`) |
| `ValidationUrl` | AEAT portal URL with all invoice params pre-filled | Yes (`result.validationUrl`) |
| `QrCodeUrl` | Direct URL to QR image on Irene Solutions server | Not captured |
| `ExternKey` | Blockchain identifier | Not captured |
| `StatusResponse` | `"Correcto"` on success | Not captured (ResultCode used instead) |

## Open issues

- CSV and QR not persisted to DB — `ingresos` needs 5 new columns (see progress tracker)
- `processResponse()` does not yet capture `QrCode` from `submitInvoice` response — currently only captured in `generateQRCode()`; merge the logic
- QR not added to printed receipt
- QR view and validation link not shown in RecogPrendas
- No retry on network failure (planned)
- No configuration UI (planned)

## Documentation

| Document | Purpose |
|----------|---------|
| `step_by_step_guide.md` | Complete step-by-step implementation guide |
| `implementation_summary.md` | DB schema SQL, security notes, class reference, roadmap |
| `rest_api.md` | Full REST API field reference |
