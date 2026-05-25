# Architecture — La Ideal

> Agent quick-start: `docs/AI_agent_instructions.md` | Find anything: `docs/INDEX.md`

## Module Overview

```
MainWindow (src/app/)
  ├── Listado           (src/listado/)           — generic list viewer (all tables)
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
  src/logging/                    — AppLogger (persistent debug log, qInstallMessageHandler)
  src/appsettings/                — AppSettings singleton + SettingsDialog
  src/sql_lite/                   — stateless DB free-function API
  src/tableview/                  — all table-view utility classes (single CMake target):
                                      TableView, MySortFilterProxyModel, FilterWidget,
                                      NumberFormatDelegate, TextColorDelegate
  QXlsx/                          — third-party Excel r/w library (do not modify)
```

---

## Module Details

### MainWindow (`src/app/`)
Central controller. Owns the SQLite `db` connection. Instantiates all child windows.

Key methods:

| Method | Purpose |
|--------|---------|
| `mainwindowInitialSettings()` | Runs `migrateDatabase()`, then UI setup (table columns, buttons) |
| `initializeVerifactu()` | Creates `VerifactuIntegration`; shows warning if not configured |
| `resetAllContents()` | Clears form after save |
| `validateTicket()` | Pre-save checks: client present, amounts consistent, quarter not locked |
| `saveTicket()` | Writes rows to `ingresos` with `verifactu_estado = PENDIENTE`; async submit patches the rows when AEAT replies |
| `verifactuSubmitInvoice(ticketNum, date, total)` | Fires `VerifactuIntegration::submitSimplifiedInvoiceAsync()`, tracks `reqId → ticketNum` in `m_pendingSubmits`, shows status bar progress |
| `onVerifactuRequestFinished(reqId, result)` | Slot — looks up the ticket, UPDATEs `verifactu_*` columns, updates status bar (success: CSV; error: description). No popups. |
| `printRecibo()` / `printFra()` | Save-time print — `verifactuIntegration = nullptr` so no QR fetch is attempted (CSV still empty). Customer reprint with QR is available via `RecogPrendas → Imprimir`. Guarded by `AppSettings::enablePrinting()`. |
| `checkClientData()` | Adds/updates client in `clientes` table on save |
| `cleanDatabase()` | Fixes decimal separators (commas→dots) in DB |
| `on_actionCrear_hash_en_ingresos_triggered()` | Backfills missing hashes in `ingresos` |

Table column constants: `TABLE_TICKET_QNTY=0`, `GARM=1`, `SIZE=2`, `SERV=3`, `OBSE=4`, `PRIC=5`

### Listado (`src/listado/`)
Generic list-view window. Set `tableName` property at runtime to display any table.
Used for: `ingresos`, `gastos`, `prendas`, `clientes`, `proveedores`, `servicios`.
Features: add row, delete row, text filter (`FilterWidget`), PDF export.
Search is diacritic-insensitive: typing "garcia" matches "García" via `MySortFilterProxyModel::setNormalizedFilter`.
Signals `populateClientes()` / `populatePrendas()` back to `MainWindow` after edits.

### RecogPrendas (`src/recog_prendas/`)
"Garment pickup" window. Loads `ingresos` rows into a filterable table view.
Operations: mark paid, mark picked up, edit observations, edit size+price, split garment rows.
Search modes: by ticket number, by phone, by date, or by client name.
Name search loads all `ingresos` and filters client-side via `MySortFilterProxyModel::setNormalizedFilter` (diacritic-insensitive — handles García, Jiménez, etc.).

### Facturas (`src/facturas/`)
Formal supplier invoice entry form. Distinct from receipts.
Writes to the `facturas` table. Populated from `empresas` and `servicios` tables.

### Contabilidad (`src/contabilidad/`)
Generates HTML accounting reports. Three modes: `Mensual`, `Trimestral`, `Anual`.
Can lock quarters to prevent further data entry (`edit_lock` in `ingresos`).
`revertirOn = true` unlocks a previously locked quarter.
Income totals call `totalPriceBetweenDates()` which excludes `verifactu_estado = 'ANULADA'` rows (cancelled invoices must not appear in taxable income). Both `ingresos` and `gastos` queries use a half-open date interval `[start, end)` to avoid double-counting on quarter boundaries.

### Imprimir (`src/imprimir/`)
Creates Excel via `QXlsx`, then launches an external process to print.
`isRecibo = true` → receipt layout; `false` → invoice layout; `isCompleteInvoice` flag adds extra fields.
Dialog accepts ticket number, fetches data from `ingresos` via `getTicketInfo()`.
General conditions block (4 clauses + RGPD notice) is printed only when `copyForClient == true`; the establishment copy omits it. Clauses are grounded in RD 1453/1987. See `docs/modules/imprimir.md` for the full layout table.

### AddGarment (`src/add_garment/`)
Adds new garment rows to an existing ticket number already in the database.

### tableview (`src/tableview/`)
Single CMake library (`tableview`) containing all table-view utility classes. All headers live in the same directory and are exposed via one INTERFACE include path.

| Class | File | Purpose |
|-------|------|---------|
| `TableView` | `tableview.h/.cpp` | `QTableView` subclass with context menu (add/delete row) and `doubleClick` signal |
| `MySortFilterProxyModel` | `mysortfilterproxymodel.h/.cpp` | `QSortFilterProxyModel` subclass; checks all columns against filter regex; also supports diacritic-insensitive plain-text filter via `setNormalizedFilter(text, column)` and `removeDiacritics(text)` static helper |
| `FilterWidget` | `filterwidget.h/.cpp` | `QLineEdit` with case-sensitivity toggle and pattern-syntax menu (Regex / Wildcard / Fixed) |
| `NumberFormatDelegate` | `numberformatdelegate.h/.cpp` | `QStyledItemDelegate` that formats numeric cells to 2 decimal places |
| `TextColorDelegate` | `textcolordelegate.h/.cpp` | `QStyledItemDelegate` that colours "SI"/"Recogido" green and "NO"/"En tienda" red |
| `LinkDelegate` | `linkdelegate.h/.cpp` | `QStyledItemDelegate` that renders non-empty cells as blue underlined text; used for the `verifactu_url_qr` column in the Ingresos Listado |

### AppLogger (`src/logging/`)
Installed once in `main()` via `AppLogger::install()`. Redirects all `qDebug`, `qWarning`, `qCritical`, and `qFatal` output to `~/.laideal.log` using `qInstallMessageHandler`. No changes required at any call site.

- Log format: `yyyy-MM-dd HH:mm:ss [DEBUG|WARN|CRIT|FATAL] <message>`
- Session separator written on each launch
- Rotates to `.laideal.log.old` when the file exceeds 5 MB; thread-safe via `QMutex`
- `AppLogger::logFilePath()` returns the path — used by `MainWindow::on_actionMostrar_log_triggered()` (Archivo → Log de depuración…) to show the customer the path with an "Abrir archivo" button that opens the log directly

### AppSettings (`src/appsettings/`)
Singleton (`AppSettings::instance()`) that loads `~/.laideal_settings.json` on startup. All modules read from it at point of use. Migrates legacy `~/.laideal_cfg` and `~/.verifactu_key` on first run.

`SettingsDialog` — 4-tab code-only dialog (no `.ui` file). Accessible from Archivo → Configuración. Writes back to the JSON file on accept.

Settings groups: `db.path`, `app.iconPath`, `app.ivaRate`, `print.enable` (bool — replaces the removed debug flag), report output paths, business name/address/city/phone (`business.phone` — used in ticket header and RGPD clause), Verifactu NIF/name/serviceKey/production.

### sql_lite (`src/sql_lite/`)
Stateless free functions; all modules include this header.

Notable items:
- `DB_PATH` macro → `dbPath()` — runtime-configured by `main()` via `setDbPath(AppSettings::instance()->dbPath())`
- `migrateDatabase(db)` — adds the 5 `verifactu_*` columns to `ingresos` via `ALTER TABLE ADD COLUMN`; idempotent (safe to call on every startup)
- `dbNotConfigured()` guard — returns early with `qWarning` if `db.databaseName()` is empty; prevents spurious error dialogs at startup
- `genHash16()` → 16-char alphanumeric hash for row deduplication (uses `QRandomGenerator`)
- `readLockForMonthAndYear()` → returns 1 if quarter is locked
- `updateLockInIngresos()` → locks/unlocks a month+year in `ingresos`
- `updateComasInDecimalData()` → data-quality fix for comma/dot decimal separator

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
| verifactu_csv | TEXT | AEAT security code (CSV) — e.g. `A-9VARYQTZTARVU2`; empty if not submitted |
| verifactu_timestamp | TEXT | ISO-8601 submission timestamp; empty if not submitted |
| verifactu_estado | TEXT | `ENVIADA` on success, `ERROR` on failure, `ANULADA` if cancelled via AEAT, `PENDIENTE` if not yet submitted (Verifactu not configured, or unpaid ticket awaiting submission at pickup). Legacy rows from before Verifactu may have NULL/empty. Use `VerifactuEstado` enum + helpers (`verifactumanager.h`) — never hardcode these strings |
| verifactu_error | TEXT | Error description if `verifactu_estado = ERROR`; empty otherwise |
| verifactu_url_qr | TEXT | AEAT ValidationUrl for QR/verification; empty if not submitted |

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

---

## Data Flow: Ticket Save

```
User fills form in MainWindow
  ↓
on_bb_save_reset_clicked(Save)
  ├── validateTicket()          — checks client, amounts, quarter lock
  ├── checkClientData()         — adds/updates client in `clientes`
  ├── verifactuSubmitInvoice()  — REST POST to AEAT; returns VerifactuResult
  │     └── on ERROR/NETWORK_ERROR: QMessageBox::warning shown in Spanish
  ├── saveTicket(result)        — writes N rows to `ingresos` + all verifactu_* columns
  ├── [if AppSettings::enablePrinting()] printRecibo()  — Excel + print receipt
  ├── [if printing enabled && paid] printFra()          — Excel + print invoice
  └── resetAllContents()        — clears form for next ticket
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

`VerifactuResult::Status` values (API call result): `SUCCESS`, `PENDING`, `ERROR`, `NETWORK_ERROR`, `INVALID_CONFIG`

`VerifactuEstado` enum class (DB-persisted state in `verifactu_estado` column): `NotSubmitted` ↔ `"PENDIENTE"`, `Enviada` ↔ `"ENVIADA"`, `Anulada` ↔ `"ANULADA"`, `Error` ↔ `"ERROR"`. Convert with `verifactuEstadoToString()` / `verifactuEstadoFromString()` (both inline in `verifactumanager.h`). `verifactuEstadoFromString()` also maps NULL/empty (legacy pre-Verifactu rows) to `NotSubmitted`.

---

## Known Issues / Technical Debt

| Issue | File | Priority | Notes |
|-------|------|----------|-------|
| ServiceKey stored in plaintext JSON | `~/.laideal_settings.json` | Medium | Consider encryption at rest |
| ~~No retry for failed Verifactu submissions~~ | ~~`src/verifactu/`~~ | — | Fixed: `retryVerifactuSubmit()` in `RecogPrendas`; save-time failure shows warning dialog |
| Clients missing from Listado but present in MainWindow combobox | `src/listado/listado.cpp`, `src/app/mainwindow.cpp` | Low | Investigate encoding/collation differences between `readColumnFromTable` and `QSqlTableModel`; tilde issue in name search is now fixed separately |

---

## Third-Party Dependencies

| Library | Location | Purpose |
|---------|----------|---------|
| QXlsx | `QXlsx/` | Excel file generation for printing |
| Qt Widgets | System | GUI framework |
| Qt Sql | System | SQLite driver |
| Qt PrintSupport | System | Print support |
| Qt Network | System | HTTP for Verifactu REST API |
