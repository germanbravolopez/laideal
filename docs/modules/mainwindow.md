# MainWindow (`src/app/`)

Central application controller. Owns the SQLite `db` connection and instantiates all child windows on demand.

## Source files

- `src/app/main.cpp` — entry point; loads font and app icon (icon path is hardcoded — known issue)
- `src/app/mainwindow.h/cpp` — main window class

## Key methods

| Method | Purpose |
|--------|---------|
| `mainwindowInitialSettings()` | UI setup at startup: runs `migrateDatabase()`, configures table columns, buttons, comboboxes |
| `initializeVerifactu()` | Creates `VerifactuIntegration`; shows non-fatal warning if not configured |
| `resetAllContents()` | Clears form after a ticket save |
| `setNextTicketNumber()` | Auto-increments `n_recibo` from the max in `ingresos` |
| `populateCbClient()` | Fills the client combobox from `clientes` table |
| `validateTicket()` | Pre-save checks: client present, garments valid, quarter not locked |
| `checkClientData()` | Adds or updates the client row in `clientes` |
| `verifactuSubmitInvoice()` | Calls `VerifactuIntegration`; returns `VerifactuResult` (INVALID_CONFIG if not configured) |
| `saveTicket(verifactuResult)` | Inserts N garment rows into `ingresos` including all 5 `verifactu_*` columns |
| `printRecibo()` / `printFra()` | Creates Excel and triggers `Imprimir` dialog |
| `cleanDatabase(print)` | Fixes comma decimal separators in DB |
| `on_actionAnular_factura_verifactu_triggered()` | Opens `CancelInvoiceDialog`; shows warning if Verifactu not configured |

## Table column indices (mainwindow.h)

| Constant | Index | Column |
|----------|-------|--------|
| `TABLE_TICKET_QNTY` | 0 | Quantity |
| `TABLE_TICKET_GARM` | 1 | Garment |
| `TABLE_TICKET_SIZE` | 2 | Size (m²) |
| `TABLE_TICKET_SERV` | 3 | Service |
| `TABLE_TICKET_OBSE` | 4 | Observations |
| `TABLE_TICKET_PRIC` | 5 | Price |

## Ticket save flow

```
on_bb_save_reset_clicked(Save)   — Qt auto-connect slot
  ├── validateTicket()
  ├── checkClientData()
  ├── verifactuSubmitInvoice()
  │     returns VerifactuResult (SUCCESS / ERROR / INVALID_CONFIG)
  ├── saveTicket(verifactuResult)  — persists all 5 verifactu_* columns
  ├── [if AppSettings::enablePrinting()] printRecibo()
  ├── [if printing enabled && paid] printFra()
  └── resetAllContents()
```

## Child windows

Opened via menu actions. Each child holds its own `db` reference set by `MainWindow` at open time:
`Listado`, `RecogPrendas`, `Facturas`, `Contabilidad`, `Imprimir`, `AddGarment`, `CancelInvoiceDialog`

`CancelInvoiceDialog` (`src/app/cancelinvoicedialog.h/cpp`) is a modal dialog (no `.ui` file) opened from Herramientas → Anular factura Verifactu. It searches `ingresos` by ticket number, shows Verifactu details, calls `VerifactuIntegration::cancelInvoice()`, and on success updates `verifactu_estado`, `verifactu_timestamp`, and `verifactu_error` for all rows of that ticket.

## Known issues

- Verifactu `m_verifactuIntegration` member is heap-allocated with `this` as Qt parent (no manual delete needed).
