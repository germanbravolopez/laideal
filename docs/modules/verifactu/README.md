# Verifactu Module (`src/verifactu/`)

AEAT mandatory digital invoicing integration (required for Spanish businesses from 2026). Submits invoices to AEAT in real time, receives a CSV security code, and generates a client QR code.

**Status**: Code complete. Pending: DB schema update and production testing.

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

// Submit an invoice
VerifactuResult result = m_verifactuIntegration->createAndSubmitInvoice(...);
// result.status: SUCCESS | PENDING | ERROR | NETWORK_ERROR | INVALID_CONFIG
```

## Configuration

Loaded from an external `.ini` file via `QSettings`. Not in source control.

| Key | Description |
|-----|-------------|
| `serviceKey` | API key — obtain at https://facturae.irenesolutions.com/verifactu/go |
| `emitterNIF` | Company NIF (confidential) |
| `environment` | `TESTING` or `PRODUCTION` |

## Environments

| Environment | Description | QR validation URL |
|-------------|-------------|-------------------|
| TESTING | Safe for dev — no real AEAT submission | `https://prewww2.aeat.es/wlpl/TIKE-CONT/ValidarQR` |
| PRODUCTION | Live — submits to AEAT | `https://www2.aeat.es/wlpl/TIKE-CONT/ValidarQR` |

REST API endpoint (both environments): `https://facturae.irenesolutions.com:8050/Kivu/Taxes/Verifactu/Invoices`

## Open issues

- CSV code received from AEAT is not persisted to DB — only printed via `qDebug()`
- DB schema missing Verifactu columns — see `RESUMEN_IMPLEMENTACION.md` for `ALTER TABLE` SQL
- No retry on network failure (planned)
- No configuration UI (planned)

## Documentation

| Document | Purpose |
|----------|---------|
| `step_by_step_guide.md` | Complete step-by-step implementation guide |
| `implementation_summary.md` | DB schema SQL, security notes, class reference, roadmap |
| `rest_api.md` | Full REST API field reference |
