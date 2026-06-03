# Architecture — La Ideal

> Agent quick-start: `docs/AI_agent_instructions.md` | Find anything: `docs/INDEX.md`

## Module Overview

```
MainWindow (src/app/)
  ├── Listado           (src/listado/)           — generic list viewer (all tables)
  ├── RecogPrendas      (src/recog_prendas/)     — garment pickup panel
  │     └── PayDialog   (src/recog_prendas/)     — partial-payment dialog (8.5+)
  ├── Facturas          (src/facturas/)          — formal supplier invoice form
  ├── Contabilidad      (src/contabilidad/)      — accounting report generator
  ├── Imprimir          (src/imprimir/)          — print / Excel generation
  ├── AddGarment        (src/add_garment/)       — add garments to existing ticket
  ├── BackupManager     (src/backup/)            — auto + manual SQLite snapshots (Verifactu Req. 4)
  ├── PendingSubmitsDialog (src/app/)            — startup recovery for verifactu_estado=PENDIENTE rows
  └── VerifactuIntegration (src/verifactu/)      — AEAT digital invoicing
         └── VerifactuManager
                ├── VerifactuConfig
                └── VerifactuInvoice + VerifactuTaxItem

  └── Updater + UpdaterDialog (src/updater/)     — in-app GitHub-releases updater

Shared infrastructure:
  src/logging/                    — AppLogger (persistent debug log, qInstallMessageHandler)
  src/appsettings/                — AppSettings singleton + SettingsDialog
  src/sql_lite/                   — stateless DB free-function API
  src/reporthtml/                 — shared A4 PDF report scaffolding (style + business header + euro format),
                                      used by Contabilidad and Listado (GenListado)
  src/tableview/                  — all table-view utility classes (single CMake target):
                                      TableView, MySortFilterProxyModel, FilterWidget,
                                      NumberFormatDelegate, TextColorDelegate
  QXlsx/                          — third-party Excel r/w library (vendored; minimal local patch: setPageMargins)
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
| `printRecibo()` / `printFra()` | Save-time print — `verifactuIntegration = nullptr` so no QR fetch is attempted (CSV still empty at save time). `createTicketExcel()` always runs (Excel file is generated even with printing disabled); the actual `printTicket()` calls are guarded by `AppSettings::enablePrinting()`. Customer can reprint a Verifactu-complete copy with QR/CSV via `RecogPrendas → Imprimir` once AEAT has replied. |
| `checkClientData()` | Adds/updates client in `clientes` table on save |
| `cleanDatabase()` | Fixes decimal separators (commas→dots) in DB |
| `on_actionCrear_hash_en_ingresos_triggered()` | Backfills missing hashes in `ingresos` |

Table column constants: `TABLE_TICKET_QNTY=0`, `GARM=1`, `SIZE=2`, `SERV=3`, `OBSE=4`, `PRIC=5`

### Listado (`src/listado/`)
Generic list-view window. Set `tableName` property at runtime to display any table.
Used for: `ingresos`, `gastos`, `prendas`, `clientes`, `proveedores`, `servicios`.
Features: add row, delete row, text filter (`FilterWidget`), PDF export.
Inline cell editing is disabled for `ingresos` (Verifactu Req. 1, Art. 8.1 RD 1007/2023): row deletion is blocked AND `setEditTriggers(NoEditTriggers)` so no column can be edited from the list view. All `ingresos` changes flow through RecogPrendas / CancelInvoiceDialog / RectifyInvoiceDialog so AEAT, the chained Huella and the accounting lock stay in sync.
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
Generates PDF accounting reports (via `src/reporthtml/` shared style). Three modes: `Mensual`, `Trimestral`, `Anual`.
Per period it prints Ingresos, Gastos and a Resumen block (Liquidación de IVA = IVA repercutido − soportado; Resultado del periodo = base ingresos − base gastos; ticket/invoice counts). The annual mode adds a consolidated summary of the four quarters. Figures are gathered once into a `PeriodFigures` struct; the period date range flows through `periodRange()`. Counts come from `sql_lite::countOperationsBetweenDates()`.
Can lock quarters to prevent further data entry (`edit_lock` in `ingresos`).
`revertirOn = true` unlocks a previously locked quarter.
Income totals call `totalPriceBetweenDates()` which excludes `verifactu_estado IN ('ANULADA', 'RECTIFICADA')` rows (cancelled invoices must not appear in taxable income, and rows superseded by a substitution rectificativa are likewise excluded so the rectifying row carries the corrected total without double-counting). Both `ingresos` and `gastos` queries use a half-open date interval `[start, end)` to avoid double-counting on quarter boundaries.

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

### Updater (`src/updater/`)
In-app updater. Two entry points: an unattended startup check (gated by `AppSettings::checkUpdatesOnStartup()`, default on) and a manual menu action (Ayuda → Buscar actualizaciones…). Both paths run through one `Updater` instance owned by `MainWindow`.

| Class | File | Purpose |
|-------|------|---------|
| `Updater` | `updater.h/.cpp` | Async REST client against the GitHub Releases API. `checkForUpdates(silentOnNoUpdate)` GETs `https://api.github.com/repos/germanbravolopez/laideal/releases/latest`, compares `tag_name` to the compile-time `PROJECT_VERSION` macro (passed in via `target_compile_definitions`), and emits `updateAvailable(version, notes, installerUrl)`, `noUpdateAvailable`, or `checkFailed`. `downloadInstaller(url)` saves the `laideal_setup_X.Y.exe` asset to `%TEMP%` and emits `downloadFinished(localPath)`. `compareVersions` is `major.minor` numeric (tolerates a leading `v`). |
| `UpdaterDialog` | `updaterdialog.h/.cpp` | Modal dialog wired to an `Updater`. Shows current vs. latest version + the GitHub release body as plain text in a `QTextEdit`. On "Actualizar ahora": collapses to a progress bar, drives `downloadInstaller`, then `QProcess::startDetached(installerPath)` + `qApp->quit()` so Inno Setup can replace the running files. |

In-place upgrade path: `releases/laideal.iss` sets `CloseApplications=yes` + `RestartApplications=yes` + `CloseApplicationsFilter=*.exe`. If the running app has not finished exiting by the time the installer starts touching `{app}\laideal.exe`, Inno detects it (filtered to .exe so we don't touch unrelated open files under `{app}`), prompts, and relaunches once install completes.

Startup-check policy: the check is fired ~1.5 s after `MainWindow` is constructed via `QTimer::singleShot` (so the main window is fully painted first), and `silentOnNoUpdate=true` — offline / GitHub down / no-update are logged at `qDebug` and never surface a popup. The manual menu action uses `silentOnNoUpdate=false` so the user always sees an outcome.

Repo visibility: `/releases/latest` requires the repo to be **public** under unauthenticated calls (private repos return 404). If the repo is ever flipped back to private the updater will fail in `silent` mode silently and surface "Not Found" via the menu action.

### BackupManager (`src/backup/`)
Closes Verifactu Req. 4 (Art. 8.2.c RD 1007/2023): durable archive of the live SQLite DB during the LGT prescription window. Single class `BackupManager` parented to `MainWindow`.

| Method | Behaviour |
|--------|-----------|
| `performBackup()` | Synchronous. Opens a dedicated `QSQLITE` connection named `laideal_backup_snapshot`, runs `VACUUM INTO '<target>'` for a transactionally-consistent snapshot without taking an exclusive lock on the live DB. Re-opens the copy read-only on a second connection (`laideal_backup_verify`) and runs `PRAGMA integrity_check`; any verdict other than `ok` deletes the file. On success, updates `AppSettings::backupLastTime` and calls `pruneOldBackups()`. Returns `{success, backupPath, errorMessage, bytesWritten}`. |
| `needsBackup(minIntervalSeconds=24*3600)` | True when `AppSettings::backupEnabled()` is on and either no `backup.last_time` is recorded or it is older than the interval. |
| `pruneOldBackups()` | Keeps every backup from the last 30 days; reduces 31-day-to-4-year backups to one per `(year, month)`; deletes everything older than 4 years. Returns the number of files removed. |
| `backupDirectory()` | Resolves the configured root, creating it if missing. Default `<DocumentsLocation>/laideal_backups`. |

Triggers wired in `MainWindow`:
- **Auto**: `MainWindow` schedules the backup via `QTimer::singleShot(3000)` if `needsBackup()` returns true, then runs `performBackup()` on a `QThread::create` worker (it uses its own dedicated DB connections and touches no GUI, so `VACUUM INTO` + `integrity_check` don't block the event loop); the `Result` is marshalled back to the GUI thread with `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`. Silent on success (status-bar message); `QMessageBox::warning` on failure — the regulatory requirement is durable storage, so a failed snapshot is the one path the operator must see.
- **Manual**: `Herramientas → Hacer copia de seguridad ahora...` (`actionHacer_copia_de_seguridad`) runs `performBackup()` under `WaitCursor` and reports path + size in a `QMessageBox` so the operator can grab the file for offsite copy.

Filename convention `laideal_yyyy-MM-dd_HHmmss.sqlite` sorts lexicographically by time, which keeps the pruner trivial. Full module reference at `docs/modules/backup.md`.

### AppLogger (`src/logging/`)
Installed once in `main()` via `AppLogger::install()`. Redirects all `qDebug`, `qWarning`, `qCritical`, and `qFatal` output to `~/.laideal.log` using `qInstallMessageHandler`. No changes required at any call site.

- Log format: `yyyy-MM-dd HH:mm:ss [DEBUG|WARN|CRIT|FATAL] <message>`
- Session separator written on each launch
- Rotates to `.laideal.log.old` when the file exceeds 5 MB; thread-safe via `QMutex`
- `AppLogger::logFilePath()` returns the path — used by `MainWindow::on_actionMostrar_log_triggered()` (Archivo → Log de depuración…) to show the customer the path with an "Abrir archivo" button that opens the log directly

### AppSettings (`src/appsettings/`)
Singleton (`AppSettings::instance()`) that loads `~/.laideal_settings.json` on startup. All modules read from it at point of use. Migrates legacy `~/.laideal_cfg` and `~/.verifactu_key` (one-time file migration) and folds obsolete in-JSON keys via `migrateLegacyKeys()` on every load.

The Verifactu **service key is encrypted at rest** with Windows DPAPI (per-user `CryptProtectData`, no prompt): `setVerifactuServiceKey()` stores `dpapi:v1:<base64>` and `verifactuServiceKey()` decrypts on read, so callers only ever see plaintext. `encryptSecretsAtRest()` runs in `load()` and re-encrypts any legacy plaintext value (then `save()`s). The blob is bound to the Windows user+machine, so it is not portable — a decrypt failure returns empty and the operator re-enters the key. Requires linking `crypt32` (Windows only).

`SettingsDialog` — 4-tab code-only dialog (no `.ui` file). Accessible from Archivo → Configuración. Writes back to the JSON file on accept.

Settings groups: `db.path`, `taxes.iva_rate`, `print.enable` (bool — guards the actual `printTicket()` call), `reports.root` (single user-configurable root; getters `contabilidadPath()` / `listadosPrendasPath()` / `listadosGastosPath()` compose `<root>/Contabilidad`, `<root>/Listados/Prendas`, `<root>/Listados/Gastos`), business name/address/city/phone, Verifactu NIF/name/serviceKey/production, `updater.check_on_startup` (bool — default `true`, drives the auto-check at app launch). The app icon is no longer a setting — it ships embedded in the executable (Windows `IDI_ICON1`) and as a Qt resource (`:/icons/laideal.ico`).

App-internal fixed paths (not user-configurable): `AppSettings::ticketExcelPath()` → `~/.laideal_ticket.xlsx` (regenerated each print), `AppSettings::ticketPrintScriptPath()` → `~/.laideal_print.vbs` (rewritten each print, templated with the xlsx path, executed via `cscript //nologo //B`).

Migration: `migrateLegacyKeys()` strips `Contabilidad` / `Listados/Prendas` / `Listados/Gastos` suffixes from any of the three legacy report-path values to derive `reports.root`; obsolete keys (`reports.contabilidad_path`, `reports.listados_prendas_path`, `reports.listados_gastos_path`, `print.template_path`, `print.script_path`) are removed via the `removeNested()` helper.

### sql_lite (`src/sql_lite/`)
Stateless free functions; all modules include this header.

Notable items:
- `DB_PATH` macro → `dbPath()` — runtime-configured by `main()` via `setDbPath(AppSettings::instance()->dbPath())`
- `migrateDatabase(db)` — adds the 9 `verifactu_*` columns to `ingresos` via `ALTER TABLE ADD COLUMN` (csv / timestamp / estado / error / url_qr / xml / hash / rectifies_n_recibo / rectification_type); idempotent (safe to call on every startup)
- `dbNotConfigured()` guard — returns early with `qWarning` if `db.databaseName()` is empty; prevents spurious error dialogs at startup
- `genHash16()` → 16-char alphanumeric hash for row deduplication (uses `QRandomGenerator`)
- `readLockForMonthAndYear()` → returns 1 if quarter is locked
- `updateLockForMonth()` → locks/unlocks a month+year in both `ingresos` and `gastos`
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
| verifactu_estado | TEXT | `ENVIADA` on success, `ERROR` on failure, `ANULADA` if cancelled via AEAT, `RECTIFICADA` if superseded by a substitution rectificativa (R1-R5 with mode S), `PENDIENTE` if not yet submitted (Verifactu not configured, or unpaid ticket awaiting submission at pickup). Legacy rows from before Verifactu may have NULL/empty. Use `VerifactuEstado` enum + helpers (`verifactumanager.h`) — never hardcode these strings |
| verifactu_error | TEXT | Error description if `verifactu_estado = ERROR`; empty otherwise |
| verifactu_url_qr | TEXT | AEAT ValidationUrl for QR/verification; empty if not submitted |
| verifactu_xml | TEXT | Raw AEAT-style XML returned by Irene Solutions (`Return.Xml`); empty if not submitted or pre-fix. Source for `Herramientas → Exportar registros AEAT (XML)...` (Art. 14.1 RD 1007/2023) |
| verifactu_hash | TEXT | 64-char hex SHA-256 chained hash (`<sum1:Huella>` extracted from `verifactu_xml`); empty if not submitted or pre-fix. Local copy of AEAT's hash-chain value for tamper-detection (Art. 12 RD 1007/2023). AEAT regulatory term is "Huella"; column is named `verifactu_hash` to keep identifiers in English |
| verifactu_rectifies_n_recibo | TEXT | On a rectificativa row, points back to the `n_recibo` of the original ticket being corrected; empty on non-rectifying rows. Combined with `verifactu_estado = RECTIFICADA` on the original gives a bidirectional audit link (Art. 8.2.a RD 1007/2023) |
| verifactu_rectification_type | TEXT | `"S"` (sustitución) or `"I"` (diferencias) on a rectificativa row; empty on non-rectifying rows. Mirrors the `RectificationType` sent to AEAT |

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
  ├── validateTicket()                       — checks client, amounts, quarter lock
  ├── checkClientData()                      — adds/updates client in `clientes`
  ├── saveTicket()                           — writes N rows to `ingresos` with verifactu_estado = PENDIENTE
  ├── if (isPaid):
  │     ├── reqId = verifactuSubmitInvoice(...)  — fires async submit; status bar "Enviando..."
  │     ├── QEventLoop, 3 s timeout              — waits for VerifactuIntegration::requestFinished(reqId, result)
  │     │   onVerifactuRequestFinished runs first (FIFO connect order) and UPDATEs ingresos
  │     │   to verifactu_estado = ENVIADA / ERROR before the lambda quits the loop
  │     └── if (result.isSuccess() && !result.qrCode.isNull()):
  │             printFra(result.qrCode)        — factura simplificada with QR + AEAT leyenda
  │         else (timeout, or success w/o QR, or error):
  │             printRecibo()                   — paid recibo: "Importe pagado" stamp on customer copy
  ├── else (not paid):
  │     └── printRecibo()                    — Excel + printTicket (claim receipt, two copies)
  └── resetAllContents()                     — clears form for next ticket

Async tail (when AEAT replies later than 3 s, or for not-paid tickets that get paid later via RecogPrendas):
  VerifactuIntegration::requestFinished(reqId, result)
  └── MainWindow::onVerifactuRequestFinished
        ├── sql_lite::updateTicketVerifactuFields(db, ticketNum, result) — UPDATE ingresos with CSV/timestamp/estado
        └── statusBar message                — "Ticket NNNN enviado (CSV: ...)" or "Error: ..."
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

`VerifactuEstado` enum class (DB-persisted state in `verifactu_estado` column): `NotSubmitted` ↔ `"PENDIENTE"`, `Enviada` ↔ `"ENVIADA"`, `Anulada` ↔ `"ANULADA"`, `Rectificada` ↔ `"RECTIFICADA"`, `Error` ↔ `"ERROR"`. Convert with `verifactuEstadoToString()` / `verifactuEstadoFromString()` (both inline in `verifactumanager.h`). `verifactuEstadoFromString()` also maps NULL/empty (legacy pre-Verifactu rows) to `NotSubmitted`.

---

## Known Issues / Technical Debt

| Issue | File | Priority | Notes |
|-------|------|----------|-------|
| ~~ServiceKey stored in plaintext JSON~~ | ~~`~/.laideal_settings.json`~~ | — | Fixed: encrypted at rest with Windows DPAPI (per-user), `dpapi:v1:` marker; auto-migrated on load. See AppSettings section |
| ~~No retry for failed Verifactu submissions~~ | ~~`src/verifactu/`~~ | — | Fixed: `retryVerifactuSubmit()` in `RecogPrendas`; save-time failure shows warning dialog |
| ~~Clients missing from Listado but present in MainWindow combobox~~ | ~~`src/listado/listado.cpp`~~ | — | Discarded: no current mechanism. `Listado::populateTable()` drains `fetchMore` for non-`ingresos` tables (loads every `clientes` row, replacing the old flaky scroll-to-bottom hack in b39cda7), and both the combobox and the view read `clientes.nombre` through the same SQLite driver so encoding cannot diverge. Reopen only if observed again. |

---

## Third-Party Dependencies

| Library | Location | Purpose |
|---------|----------|---------|
| QXlsx | `QXlsx/` | Excel file generation for printing |
| Qt Widgets | System | GUI framework |
| Qt Sql | System | SQLite driver |
| Qt PrintSupport | System | Print support |
| Qt Network | System | HTTP for Verifactu REST API |
| Qt Test | System | Unit / integration tests (`tests/`) |

---

## Testing

`tests/` holds Qt Test suites registered with CTest (`enable_testing()` + `add_subdirectory(tests)` in the root `CMakeLists.txt`; `Test` added to the root `find_package`). They build as part of the normal build and run with `ctest --test-dir build --output-on-failure`. Both use `QTEST_GUILESS_MAIN` (QCoreApplication) so they run headless on CI without a display.

- **`test_sql_lite`** — links the `sql_lite` static lib and exercises its free functions against a throwaway SQLite DB created in a `QTemporaryDir` (schema created in `initTestCase`, tables cleared in `init()` before each test). Covers accounting totals + Verifactu estado filters (`totalPriceBetweenDates`, `countOperationsBetweenDates`), the accounting locks (`readLockForMonthAndYear`, `readLockForQuarter`), `nextVerifactuInvoiceSeq`, `verifactuInvoiceId`, `garmentImporte`, `readClientPhones`, `removeSpecialChars`, `genHash16`, and the `RecogPrendas::updateDb` DB-write seams (`updateTicketPayment`/`Pickup`/`Observations`/`SizeAndPrice`, `updateGarmentQtyAndImporte`, `insertGarmentRow`, `ticketVerifactuEstado` — asserted to be scoped by `(n_recibo, hash)` so a multi-row ticket's siblings stay untouched). Fixtures stay on success paths + dot-decimal importes so the functions' modal-`QMessageBox` error paths never fire under the guiless main.
- **`test_mysortfilterproxymodel`** — links `tableview`, covers `removeDiacritics`, accent-insensitive `filterAcceptsRow`, and the `lessThan` sort comparators (chronological dates, numeric importe, locale-string fallback).
- **`test_verifactu_models`** — links `verifactu`, covers the pure model classes: `VerifactuConfig` validation + environment URLs, `VerifactuTaxItem` JSON/operation-type, `VerifactuInvoice` JSON/totals/validation/rectificativa, and the `verifactu_estado` string round-trip.
- **`test_verifactu_response`** — links `verifactu` and covers `parseVerifactuResponse()` (the pure AEAT/Irene-Solutions JSON parser extracted from `VerifactuManager` into `verifacturesponse.{h,cpp}` so it is testable without a network): error/success shapes, `Huella` hash extraction, and the base64->QPixmap QR decode. Uses `QTEST_MAIN` under `QT_QPA_PLATFORM=offscreen` (set via the CTest `ENVIRONMENT` property) because QPixmap needs a QGuiApplication.
- **`test_backup_manager`** — links `backup` and covers `BackupManager::backupsToPrune()` (the pure retention decision split out of `pruneOldBackups()`): recent-kept, one-per-month, beyond-4-years dropped, non-schema names ignored.
- **`test_contabilidad`** — links `contabilidad` and covers `Contabilidad::periodRangeFor()` (the pure period-range date math split out of the `periodRange()` member): half-open quarter/month ranges incl. the year-boundary roll-over.
- **`test_facturas`** — links `facturas`, covers the pure IVA split `taxBaseFromGross` / `taxAmountFromGross`.
- **`test_genlistado`** — links `listado`, covers `GenListado::filenameSuffix` and `shouldPrintGastoRow` (the pure bits of the gastos-listado slots).
- **`test_appsettings`** — links `appsettings`, covers the DPAPI secret-at-rest helpers (`dpapiEncrypt`/`dpapiDecrypt`/`dpapiIsEncrypted`): marker, encrypt↔decrypt round-trip, legacy-plaintext passthrough.
- **`test_reporthtml`** — links `reporthtml`, covers `ReportHtml::formatEuro` / `tableOpen` / `documentClose`.
- **`test_versioncompare`** (suite `TestUpdater`) — links `updater`, covers `Updater::compareVersions` / `currentVersion`. NB the executable must not be named `*update*`/`*setup*`/`*install*`: Windows' UAC installer-detection heuristic flags such names as requiring elevation and CTest then fails with `BAD_COMMAND`.

CI (`ci.yml`) runs `ctest` on every push/PR; `release.yml` runs it as a **hard gate** between Build and Stage, so a failing test aborts the publish. Each suite runs with `-o build/test-results-<suite>.xml,junitxml`; both workflows then call `.github/scripts/Render-TestSummary.ps1` to render a foldable per-suite, per-method pass/fail table into the run's step summary (the script reads stdout-vs-`GITHUB_STEP_SUMMARY` so it also runs locally). New coverage should follow the same shape: link the production static lib, drive it through real inputs, assert observable results.
