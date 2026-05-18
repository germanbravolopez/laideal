# MainWindow (`src/app/`)

Central application controller. Owns the SQLite `db` connection and instantiates all child windows on demand.

## Source files

- `src/app/main.cpp` — entry point; loads font and app icon (icon path is hardcoded — known issue)
- `src/app/mainwindow.h/cpp` — main window class

## Key methods

| Method | Purpose |
|--------|---------|
| `mainwindowInitialSettings()` | UI setup at startup: table columns, buttons, comboboxes |
| `initializeVerifactu()` | Creates `VerifactuIntegration`; shows non-fatal warning if not configured |
| `resetAllContents()` | Clears form after a ticket save |
| `setNextTicketNumber()` | Auto-increments `n_recibo` from the max in `ingresos` |
| `populateCbClient()` | Fills the client combobox from `clientes` table |
| `validateTicket()` | Pre-save checks: client present, garments valid, quarter not locked |
| `checkClientData()` | Adds or updates the client row in `clientes` |
| `verifactuSubmitInvoice()` | Calls `VerifactuIntegration`; gracefully skips if not configured |
| `showQrToClient(result)` | Modal dialog showing QR image + CSV + AEAT validation URL |
| `saveTicket()` | Inserts N garment rows into `ingresos`; returns `true` if paid |
| `printRecibo()` / `printFra()` | Creates Excel and triggers `Imprimir` dialog |
| `cleanDatabase(print)` | Fixes comma decimal separators in DB |

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
  ├── verifactuSubmitInvoice() → showQrToClient() on success
  ├── saveTicket()
  ├── printRecibo()
  ├── [if paid] printFra()
  └── resetAllContents()
```

## Child windows

Opened via menu actions. Each child holds its own `db` reference set by `MainWindow` at open time:
`Listado`, `RecogPrendas`, `Facturas`, `Contabilidad`, `Imprimir`, `AddGarment`

## Known issues

- **Temp debug code** (`mainwindow.cpp` near top of constructor): calls `verifactuSubmitInvoice()` then `std::exit(0)` — remove before release.
- `main.cpp`: hardcoded app icon path pointing to a specific user's machine.
- Verifactu `m_verifactuIntegration` member is heap-allocated with `this` as Qt parent (no manual delete needed).
