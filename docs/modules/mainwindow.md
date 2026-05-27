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
| `verifactuSubmitInvoice(ticketNum, date, total)` | Fires `VerifactuIntegration::submitSimplifiedInvoiceAsync()`, tracks `reqId → ticketNum` in `m_pendingSubmits`, shows status-bar progress |
| `onVerifactuRequestFinished(reqId, result)` | Slot — looks up the ticket, UPDATEs `verifactu_*` columns, updates status bar |
| `saveTicket()` | Inserts N garment rows into `ingresos` with `verifactu_estado = PENDIENTE`; async submit patches the rows when AEAT replies |
| `printRecibo()` / `printFra()` | Creates Excel and triggers `Imprimir`. `verifactuIntegration = nullptr` so no QR fetch at save time. Excel is generated unconditionally; `printTicket()` runs only when `AppSettings::enablePrinting()` is true. |
| `cleanDatabase(print)` | Fixes comma decimal separators in DB |
| `on_actionAnular_factura_verifactu_triggered()` | Opens `CancelInvoiceDialog`; shows warning if Verifactu not configured |
| `on_actionRectificar_factura_verifactu_triggered()` | Opens `RectifyInvoiceDialog` (R1-R5 factura rectificativa); shows warning if Verifactu not configured. Art. 8.2.a RD 1007/2023 |
| `on_actionAcerca_de_Verifactu_triggered()` | Opens the Ayuda → Acerca de Verifactu dialog showing the fixed-text declaración responsable required by Art. 13 RD 1007/2023. Producer NIF/name/address come from `AppSettings`; software version comes from `PROJECT_VERSION_MAJOR/MINOR` in the generated `version.h` |

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
  ├── saveTicket()                  — N rows with verifactu_estado = PENDIENTE
  ├── if (isPaid):
  │     ├── verifactuSubmitInvoice  — fires async; status bar "Enviando..."
  │     └── printFra()              — factura simplificada (no CSV/QR yet)
  ├── else:
  │     └── printRecibo()           — claim receipt, two copies
  └── resetAllContents()
```

When the AEAT reply arrives, `onVerifactuRequestFinished()` UPDATEs `verifactu_csv` / `verifactu_timestamp` / `verifactu_estado` for the ticket and posts the result to the status bar. The save-time print never has the CSV/QR; the customer can reprint a Verifactu-complete copy via `RecogPrendas → Imprimir` once status arrives.

## Child windows

Opened via menu actions. Each child holds its own `db` reference set by `MainWindow` at open time:
`Listado`, `RecogPrendas`, `Facturas`, `Contabilidad`, `Imprimir`, `AddGarment`, `CancelInvoiceDialog`, `RectifyInvoiceDialog`

`CancelInvoiceDialog` (`src/app/cancelinvoicedialog.h/cpp`) is a modal dialog (no `.ui` file) opened from Herramientas → Anular factura Verifactu. It searches `ingresos` by ticket number, shows Verifactu details, calls `VerifactuIntegration::cancelInvoiceAsync()`, and on success updates `verifactu_estado` to `ANULADA` for all rows of that ticket.

`RectifyInvoiceDialog` (`src/app/rectifyinvoicedialog.h/cpp`) is a modal dialog (no `.ui` file) opened from Herramientas → Rectificar factura Verifactu. It searches `ingresos` by ticket number, lets the operator pick R1-R5 + sustitución/diferencias (S/I) + corrected total or delta + date, calls `VerifactuIntegration::submitRectificationAsync()`, and on success inserts a NEW `ingresos` row with the next available `n_recibo`, `verifactu_estado = ENVIADA`, `verifactu_rectifies_n_recibo` pointing back to the original and the rectification's own CSV/XML/hash. For substitution mode (`S`) it additionally marks the original rows `verifactu_estado = RECTIFICADA` so they are excluded from `totalPriceBetweenDates()`. For differences (`I`) the original rows stay `ENVIADA` and the delta row alone reconciles accounting.

## Known issues

- Verifactu `m_verifactuIntegration` member is heap-allocated with `this` as Qt parent (no manual delete needed).
