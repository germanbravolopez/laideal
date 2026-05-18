# Architecture — La Ideal

> Agent quick-start: `docs/AI_agent_instructions.md` | Find anything: `docs/INDEX.md`

## Module Overview

```
MainWindow (src/app/)
  ├── Listado           (src/Listado/)          — generic list viewer (all tables)
  ├── RecogPrendas      (src/recog_prendas/)     — garment pickup panel
  ├── Facturas          (src/facturas/)          — formal supplier invoice form
  ├── Contabilidad      (src/contabilidad/)      — accounting report generator
  ├── Imprimir          (src/imprimir/)          — print / Excel generation
  ├── AddGarment        (src/add_garment/)       — add garments to existing ticket
  └── VerifactuIntegration (src/verifactu/)      — AEAT digital invoicing
         └── VerifactuManager
                ├── VerifactuConfig
                └── VerifactuInvoice + VerifactuTaxItem

Shared infrastructure:
  src/sql_lite/                   — stateless DB free-function API
  src/MySortFilterProxyModel/     — sort+filter proxy used by all list views
  src/FilterWidget/               — reusable text filter widget
  src/TableView/                  — custom QTableView subclass
  src/NumberFormatDelegate/       — table cell number formatting
  src/TextColorDelegate/          — table cell text coloring
  QXlsx/                          — third-party Excel r/w library (do not modify)
```

---

## Module Details

### MainWindow (`src/app/`)
Central controller. Owns the SQLite `db` connection. Instantiates all child windows.

Key methods:

| Method | Purpose |
|--------|---------|
| `mainwindow_initial_settings()` | UI setup at startup (table columns, buttons) |
| `initialize_verifactu()` | Creates `VerifactuIntegration`; shows warning if not configured |
| `reset_all_contents()` | Clears form after save |
| `validate_ticket()` | Pre-save checks: client present, amounts consistent, quarter not locked |
| `save_ticket()` | Writes rows to `ingresos`; returns `true` if paid |
| `verifactu_submit_invoice()` | Calls `VerifactuIntegration` with ticket data |
| `show_qr_to_client()` | Modal showing QR image + CSV + AEAT validation URL |
| `print_recibo()` / `print_fra()` | Create Excel and trigger `Imprimir` |
| `check_client_data()` | Adds/updates client in `clientes` table on save |
| `limpiar_base_de_datos()` | Fixes decimal separators (commas→dots) in DB |
| `on_actionCrear_hash_en_ingresos_triggered()` | Backfills missing hashes in `ingresos` |

Table column constants: `TABLE_TICKET_QNTY=0`, `GARM=1`, `SIZE=2`, `SERV=3`, `OBSE=4`, `PRIC=5`

### Listado (`src/Listado/`)
Generic list-view window. Set `table_name` property at runtime to display any table.
Used for: `ingresos`, `gastos`, `prendas`, `clientes`, `proveedores`, `servicios`.
Features: add row, delete row, text filter (`FilterWidget`), PDF export.
Signals `populate_clientes()` / `populate_prendas()` back to `MainWindow` after edits.

### RecogPrendas (`src/recog_prendas/`)
"Garment pickup" window. Loads all `ingresos` rows into a filterable table view.
Operations: mark paid, mark picked up, edit observations, edit size+price, split garment rows.
Search: by client name and by date range.

### Facturas (`src/facturas/`)
Formal supplier invoice entry form. Distinct from receipts.
Writes to the `facturas` table. Populated from `empresas` and `servicios` tables.

### Contabilidad (`src/contabilidad/`)
Generates HTML accounting reports. Three modes: `Mensual`, `Trimestral`, `Anual`.
Can lock quarters to prevent further data entry (`edit_lock` in `ingresos`).
`revertir_on = true` unlocks a previously locked quarter.

### Imprimir (`src/imprimir/`)
Creates Excel via `QXlsx`, then launches an external process to print.
`is_recibo = true` → receipt layout; `false` → invoice layout; `is_complete_invoice` flag adds extra fields.
Dialog accepts ticket number, fetches data from `ingresos` via `get_ticket_info()`.

### AddGarment (`src/add_garment/`)
Adds new garment rows to an existing ticket number already in the database.

### sql_lite (`src/sql_lite/`)
Stateless free functions; all modules include this header.

Notable items:
- `DB_PATH` macro → hardcoded to `C:/Users/rocio/OneDrive/Desktop/Tintoreria/BaseDatos/laideal.db`
- `gen_hash_16()` → 16-char alphanumeric hash for row deduplication
- `read_lock_for_month_and_year()` → returns 1 if quarter is locked
- `update_lock_in_ingresos()` → locks/unlocks a month+year in `ingresos`
- `update_comas_in_decimal_data()` → data-quality fix for comma/dot decimal separator

---

## Database Schema

### `ingresos` (income / ticket rows)

| Column | Type | Notes |
|--------|------|-------|
| n_recibo | INTEGER | Receipt number — shared across all garment rows of one ticket |
| cliente | TEXT | Client name (free text) |
| fecha_recepcion | TEXT | Reception date, format `DD-MM-YYYY` |
| fecha_pago | TEXT | Payment date `DD-MM-YYYY`; blank if unpaid |
| fecha_recogida | TEXT | Pickup date `DD-MM-YYYY`; blank until picked up |
| importe | REAL | Price for this garment row |
| pagado | TEXT | `"SI"` or `"NO"` |
| estado | TEXT | `"En tienda"` or similar |
| cantidad | INTEGER | Number of garments in this row |
| prenda | TEXT | Garment name (FK → `prendas.nombre`) |
| size | REAL | Size multiplier (m² for rugs, etc.) |
| servicio | TEXT | `"Limp."` (cleaning) or `"Plan."` (ironing) |
| observaciones | TEXT | Free-text notes |
| edit_lock | INTEGER | `0` = editable, `1` = locked by accounting close |
| hash | TEXT | 16-char deduplication hash |

### `prendas` (garment catalogue)

| Column | Notes |
|--------|-------|
| nombre | Garment name |
| precio_limpieza | Unit price for "Limp." service |
| precio_plancha | Unit price for "Plan." service |

### `clientes`

| Column | Notes |
|--------|-------|
| nombre | Client name (primary identifier) |
| tel_fijo | Landline phone |
| movil | Mobile phone |
| direccion | Address |

### `gastos` (expenses)
Columns include `importe` (REAL) and supplier/date/description fields. Managed via Listado.

### Other tables
`proveedores`, `servicios`, `facturas`, `empresas` — managed via Listado or Facturas windows.

### Pending schema changes (Verifactu)
See `docs/modules/verifactu/RESUMEN_IMPLEMENTACION.md` for the `ALTER TABLE` SQL to add Verifactu tracking columns to `ingresos`/`facturas`.

---

## Data Flow: Ticket Save

```
User fills form in MainWindow
  ↓
on_bb_save_reset_clicked(Save)
  ├── validate_ticket()          — checks client, amounts, quarter lock
  ├── check_client_data()        — adds/updates client in `clientes`
  ├── verifactu_submit_invoice() — REST POST to AEAT (fails gracefully)
  │     └── show_qr_to_client()  — shows QR dialog on success
  ├── save_ticket()              — writes N rows to `ingresos`
  ├── print_recibo()             — Excel + print receipt
  ├── [if paid] print_fra()      — Excel + print invoice
  └── reset_all_contents()       — clears form for next ticket
```

---

## Verifactu Integration

Full implementation docs: `docs/modules/verifactu/`

```
VerifactuIntegration   (facade, used directly by MainWindow)
  └── VerifactuManager (HTTP REST via QNetworkAccessManager)
        ├── VerifactuConfig    (QSettings INI — serviceKey, NIF, environment)
        └── VerifactuInvoice   (JSON-serialisable invoice model)
              └── VerifactuTaxItem (per-rate tax line)
```

API endpoint (prod & test use same URL):
`https://facturae.irenesolutions.com:8050/Kivu/Taxes/Verifactu/Invoices`

AEAT QR validation:
- Production: `https://www2.aeat.es/wlpl/TIKE-CONT/ValidarQR`
- Testing: `https://prewww2.aeat.es/wlpl/TIKE-CONT/ValidarQR`

`VerifactuConfig` loads from an external `.ini` file via `QSettings`. The `ServiceKey` must be obtained from https://facturae.irenesolutions.com/verifactu/go and is not in source control.

`VerifactuResult` status values: `SUCCESS`, `PENDING`, `ERROR`, `NETWORK_ERROR`, `INVALID_CONFIG`

---

## Known Issues / Technical Debt

| Issue | File | Priority | Notes |
|-------|------|----------|-------|
| **Temp debug exit in constructor** | `src/app/mainwindow.cpp:23–27` | **Critical** | Remove `verifactu_submit_invoice()` + `std::exit(0)` before release |
| **Verifactu CSV not persisted** | `src/app/mainwindow.cpp:320` | High | CSV received but not written to DB |
| **DB missing Verifactu columns** | `ingresos`/`facturas` tables | High | `ALTER TABLE` SQL in verifactu docs |
| Hardcoded DB path | `src/sql_lite/sql_lite.h:9` | Medium | Requires manual edit for each new machine |
| Hardcoded icon path | `src/app/main.cpp:12–13` | Medium | Same issue |
| ServiceKey stored plaintext | `src/verifactu/verifactuconfig.h` | Medium | Consider QSettings encryption |
| No retry for failed Verifactu submissions | `src/verifactu/` | Low | Roadmap v1.1 |

---

## Third-Party Dependencies

| Library | Location | Purpose |
|---------|----------|---------|
| QXlsx | `QXlsx/` | Excel file generation for printing |
| Qt Widgets | System | GUI framework |
| Qt Sql | System | SQLite driver |
| Qt PrintSupport | System | Print support |
| Qt Network | System | HTTP for Verifactu REST API |
