# Architecture - La Ideal

Agent briefing: [ai_agent_instructions.md](ai_agent_instructions.md). Find anything: [INDEX.md](INDEX.md). Per-module detail is in `docs/modules/`; this page holds what spans modules.

## Modules

```
MainWindow (src/app/, built in code, no .ui file)
  |- Listado           src/listado/        generic list viewer (all tables)
  |- RecogPrendas      src/recog_prendas/  garment pickup; PayDialog, VoidGarmentsDialog
  |- AddGarment        src/add_garment/    add garments to an existing ticket
  |- Facturas          src/facturas/       supplier-invoice form (writes gastos)
  |- Contabilidad      src/contabilidad/   accounting reports, quarter close
  |- Imprimir          src/imprimir/       print orchestration
  |     `- printing    src/printing/       ESC/POS builder, renderer, RAW spooler, Status API
  |- BackupManager     src/backup/         automatic and manual SQLite snapshots
  |- Updater           src/updater/        GitHub-releases updater and its dialog
  |- app dialogs       src/app/            Cancel / Rectify invoice, Pending submits, Refused invoices,
  |                                        AEAT export, SelfCheck, InstanceLock
  `- VerifactuIntegration  src/verifactu/  facade over two backends (VerifactuBackend)
        |- VerifactuManager   gateway (Irene Solutions REST): VerifactuConfig, VerifactuInvoice, VerifactuTaxItem
        `- AeatDirectBackend  direct client (SOAP + certificate)
              |- AeatRecord, AeatHash, AeatQr          record XML, huella, local QR
              |- AeatStore, AeatChainCheck             aeat_* tables, stored-chain verification
              |- AeatCertificate, AeatTransport        certificate, WinHTTP SOAP POST
              `- AeatSelfTest, AeatConnectionProbe     test-environment rehearsal, query-only connection test

Shared:
  src/logging/      AppLogger: persistent debug log
  src/appsettings/  AppSettings singleton, AppLanguage (leaf: links only Qt Core)
  src/settingsdialog/  SettingsDialog (links appsettings, uikit, printing)
  src/sql_lite/     stateless database functions, schema, migrations, shared helpers
  src/reporthtml/   A4 PDF report scaffolding and the one PDF writer (Contabilidad, GenListado)
  src/uikit/        shared dialog style
  src/tableview/    TableView, MySortFilterProxyModel, FilterWidget, delegates (one CMake library)
```

Module references: [mainwindow](modules/mainwindow.md), [sql_lite](modules/sql_lite.md), [listado](modules/listado.md), [recog_prendas](modules/recog_prendas.md), [add_garment](modules/add_garment.md), [facturas](modules/facturas.md), [contabilidad](modules/contabilidad.md), [imprimir](modules/imprimir.md), [printing](modules/printing.md), [backup](modules/backup.md), [uikit](modules/uikit.md), [verifactu](modules/verifactu/README.md).

### Library layering

- Each `src/<module>/` is a static library; `src/app/` builds `laideal_app` (everything but `main.cpp`) and the `laideal` executable, so the end-to-end bench can link and drive the windows.
- `verifactu_types` is a header-only INTERFACE target (`verifactutypes.h`: estados, `VerifactuResult`, `splitGross`). `sql_lite`, `add_garment`, `facturas`, `contabilidad` and `printing` link only it, so the base layer never links the Verifactu library or Qt Network.
- `printing` is also linked by `settingsdialog` (the "Probar Status API" button). `listado` links `contabilidad` (the IVA rates a gasto may have) and `tableview`.
- The app reaches the direct client through `VerifactuIntegration` only (`isDirect`, `holdsPendingRegistration`, `storedRegistrationOutcome`, `environmentName`, `sendingError`, `certificateExpiryWarning`, `verifyStoredChain`, `syncDirectChain`). `directBackend()` remains for the Configuración self-test, the AEAT export, the self-check and the tests.

## Modules without their own page

### AppSettings (`src/appsettings/`)

- `AppSettings::instance()` loads `~/.laideal_settings.json` at startup; modules read it at the point of use. The keys are listed in the [README](../README.md#configuration) and defined in `appsettings.cpp`; the Verifactu keys, with their formats, in [verifactu/README.md](modules/verifactu/README.md).
- Saves are atomic (`QSaveFile`). A file that does not parse is kept as `.laideal_settings.json.corrupt-<timestamp>` before defaults apply. `loadReadOnly(path)` loads in memory only, for `--self-check`.
- Legacy `~/.laideal_cfg`, `~/.verifactu_key` and obsolete JSON keys are migrated or removed on load (`migrateLegacyKeys`, `removeNested`).
- The service key and the certificate password are encrypted at rest with Windows DPAPI (`dpapi:v1:<base64>`); setters encrypt, getters decrypt, `encryptSecretsAtRest()` upgrades legacy plaintext on load. If DPAPI fails the value stays plaintext with a `qWarning` and is retried at the next load. The blob is bound to the Windows user and machine, so a decrypt failure returns empty and the operator re-enters the secret.
- The ingresos IVA rate is the constant `AppSettings::kIngresosIvaRate` (21 %), returned by `AppSettings::ivaRate()`; no key holds it.
- `AppLanguage` (`applanguage.*`): `applyQtTranslations()` installs or removes Qt's `qtbase_es.qm`; `releaseNotesResource()` picks the bundled release-notes file; on first run `initialLanguage(installerChoice())` reads the installer's choice from `HKCU\Software\La Ideal\Language`.
- `SettingsDialog` (`src/settingsdialog/`): code-built, four tabs, rebuilt on every open, writes the JSON on accept. Accepting warns without blocking where it can (missing database file, certificate not in the store, unreadable stored secret). The production switches ask for confirmation in and out.

### AppLogger (`src/logging/`)

`AppLogger::install()` in `main()` redirects `qDebug` / `qWarning` / `qCritical` / `qFatal` to `~/.laideal.log` through `qInstallMessageHandler`; no call site changes.

- Format `yyyy-MM-dd HH:mm:ss [DEBUG|WARN|CRIT|FATAL] <message>`; a separator line per launch.
- Rotates to `.laideal.log.old` above 5 MB, also during a session. If the log cannot be opened it falls back to `<temp>/laideal.log`, else logs nothing. Thread-safe (`QMutex`); `installAt(path, maxBytes)` is the test seam.
- `logFilePath()` feeds Ayuda -> Log de depuración.

### tableview (`src/tableview/`)

| Class | Purpose |
|-------|---------|
| `TableView` | `QTableView` with an add / delete row context menu and a `doubleClick` signal |
| `MySortFilterProxyModel` | `setSearchText(text, column)`: accents and case ignored, every word must appear in a searched column |
| `FilterWidget` | `QLineEdit` with a clear button and a "Buscar..." placeholder |
| `NumberFormatDelegate` | Numeric cells with two decimals |
| `TextColorDelegate` | Green / red for "SI" / "NO" and "Recogido" / "En tienda"; a voided or `ANULADA` row reads green; the pure rule is `classify(cellText, rowEstado, rowVerifactuEstado)` |
| `LinkDelegate` | Blue underlined text, for `verifactu_url_qr` |
| `placeIngresosDateColumns()` | Moves `fecha_anulacion` visually after the other dates; logical `INGRESOS_COL_*` indices are unchanged |

### Updater (`src/updater/`)

`Updater` is a GitHub Releases client owned by `MainWindow`; `UpdaterDialog` drives it. Two entry points: a silent check about 1.5 s after the window is built (gated by `updater.check_on_startup`; offline or no update only logs) and Ayuda -> Buscar actualizaciones (always reports an outcome).

- `checkForUpdates` reads `/releases/latest` (needs a public repository, drafts and pre-releases are invisible), compares `tag_name` to `PROJECT_VERSION` (`major.minor`, an optional leading `v`; a tag that is not exactly `X.Y` is a failed check, never "newer").
- `downloadInstaller` takes the first `laideal_setup_*.exe` asset to the per-user app-data folder as `.part`, renames it, and emits `downloadFinished` only when its size and SHA-256 match the asset `digest` or the `.sha256` asset. The URL and every redirect must be https on `github.com` or `*.githubusercontent.com`. There is no code-signature check.
- The dialog shows the release body as plain text, can cancel the download, then starts the installer detached with `TMP` / `TEMP` redirected (for the child only) to the folder it lives in, and quits. The installer is per-user (`PrivilegesRequired=lowest`), so it runs non-elevated and inherits the redirect; this works around machines whose `%TEMP%` denies execute. `releases/laideal.iss` sets `CloseApplications=yes` and `RestartApplications=yes` for the in-place upgrade.

## Data flows

### Ticket save

```
on_pb_save_clicked()                      ignored while a save is running
  |- validateTicket()                     client, chain and invoicing gates, row amounts, quarter lock
  |- saveTicket()                         N rows into ingresos, all or none:
  |                                       PENDIENTE when paid, SIN COBRAR when not
  |- checkClientData()                    add or update the client
  |- submitAndPrint()
  |    |- not paid: printRecibo()
  |    `- paid:  verifactuSubmitInvoice()      async submit, tracked in m_pendingSubmits
  |         |- direct client: QR computed locally (qrBeforeReply) -> printFra() at once
  |         `- gateway: wait up to 3 s for requestFinished (menus and form disabled)
  |              success with QR -> printFra(); otherwise printRecibo() (paid stamp)
  `- resetAllContents()

Later replies (after the 3 s, or a payment made later in Recogida):
  onVerifactuRequestFinished()   updateTicketVerifactuFields(ticket, seq) on the paid rows of that event
  onRecordSettled()              direct client: a record settled outside the save flow
                                 (applySettledVerifactuResult / applySettledVerifactuCancellation)
```

Printing runs `buildTicket()` always (the ESC/POS bytes are built even with printing off); `printTicket()` is guarded by `print.enable`. The factura's QR is the pixmap the save flow hands to `printFra`; a copy with QR can be reprinted from Recogida once AEAT has replied. More: [mainwindow.md](modules/mainwindow.md), [imprimir.md](modules/imprimir.md).

### Rules that span modules

- `ingresos` is never edited or deleted from the list view. Every change goes through RecogPrendas, CancelInvoiceDialog, VoidGarmentsDialog or RectifyInvoiceDialog so AEAT, the hash chain and the quarter lock stay in step. The database enforces part of it: triggers refuse deleting a row registered at AEAT and changing the invoice identity of an accepted one.
- A row is locked when its date (`fecha_pago` for `ingresos`, `fecha` for `gastos`) is in a closed quarter; closing writes nothing to `ingresos`.
- Income excludes `ANULADA` and `RECTIFICADA` rows; a paid row with a `fecha_anulacion` is subtracted in that date's period ([contabilidad.md](modules/contabilidad.md)). Date ranges are half-open `[start, end)`.

## Database schema

The shop's database predates some columns; `migrateDatabase` (idempotent, run at every start) adds what is missing. Table creation for an empty file: `kBaseSchema` in `sql_lite.cpp`. Dates are text, `dd-MM-yyyy` (`parseDbDate`, `dbDateText`). Column indices: `ingresos_schema.h`.

### `ingresos`

| Column | Notes |
|--------|-------|
| n_recibo | Ticket number, shared by all rows of a ticket; numeric text (compare as `CAST(n_recibo AS INTEGER)`) |
| cliente | Client name, free text |
| fecha_recepcion / fecha_pago / fecha_recogida | Dates; payment and pickup blank until they happen |
| importe | Price of the row as text (`"12.50"`; a legacy comma reads as a point, `INGRESOS_IMPORTE_VALUE`) |
| pagado | `SI` / `NO` |
| estado | `En tienda`, `Recogido`, `Anulado` |
| cantidad, prenda, size, servicio, observaciones | Quantity, garment (name in `prendas`), size multiplier (m2), `Limp.` / `Plan.`, notes |
| edit_lock | Legacy lock flag, read once by the migration that built `accounting_closures`; every INSERT still writes `'0'` (the shop's column is NOT NULL without default) |
| hash | 16-character id of the row within its ticket (`genHash16`); unique per `(n_recibo, hash)` |
| verifactu_csv, verifactu_timestamp | AEAT's security code and the submission time |
| verifactu_estado | See below |
| verifactu_error | Error text when the estado is `ERROR` |
| verifactu_url_qr | AEAT verification URL of the QR |
| verifactu_xml | AEAT record XML, source of Facturas -> Exportar registros AEAT |
| verifactu_hash | 64-hex SHA-256 chained hash (the Huella) |
| verifactu_rectifies_n_recibo, verifactu_rectification_type | On a rectificativa row: the corrected ticket, and `S` (sustitucion) or `I` (diferencias) |
| verifactu_invoice_seq | Payment event number within the ticket (0 for the first or legacy) |
| verifactu_invoice_id | The InvoiceID submitted for the row |
| fecha_anulacion | Date the row was voided, cancelled at AEAT or superseded by a substitution; never overwritten once set |
| verifactu_cancel_xml | AEAT's cancellation record, on the cancelled event's paid rows; exported as `<Anulacion>` |

`verifactu_estado` (`VerifactuEstado` in `verifactutypes.h`; never hardcode the strings):

| Value | Enum | Meaning |
|-------|------|---------|
| `SIN COBRAR` | `Unpaid` | Unpaid, no invoice to send yet |
| `PENDIENTE` | `NotSubmitted` | Paid and due at AEAT, not yet confirmed; also legacy NULL / empty |
| `ENVIADA` | `Enviada` | Accepted by AEAT |
| `ERROR` | `Error` | Submission or cancellation failed |
| `ANULADA` | `Anulada` | Cancelled at AEAT |
| `RECTIFICADA` | `Rectificada` | Superseded by a substitution rectificativa (R1-R5, mode S) |
| other text | `Unknown` | Never resent, cancelled or rectified |

Convert with `verifactuEstadoToString` / `verifactuEstadoFromString`. Gate on `verifactuEstadoIsUnsubmitted()` (true for `SIN COBRAR`, `PENDIENTE` and legacy blank); only the start-up recovery dialog tells the first two apart. A paid row can still hold `SIN COBRAR`: paying does not rewrite the estado.

### Other tables

| Table | Content |
|-------|---------|
| `prendas` | `nombre`, `precio_limpieza`, `precio_plancha` |
| `clientes` | `nombre`, `tel_fijo`, `movil`, `direccion` |
| `proveedores` | `nombre`, `cif`, `direccion`, `telefono` |
| `servicios` | `nombre` |
| `gastos` | Supplier invoices: `id`, `n_factura`, `servicio`, `descripcion`, `empresa`, `fecha`, `iva`, `importe`, legacy `edit_lock` |
| `accounting_closures` | One row per quarter close: `year`, `quarter`, `closed_at`, `reopened_at` (NULL while closed), `origin` (`contabilidad` or `migration`), `report_file`. One open row per quarter (partial unique index). Append-only: triggers refuse DELETE and any UPDATE other than setting `reopened_at` once |
| `migration_notes` | Review notices already logged, so each is logged once |

### Direct AEAT client tables

Created by `AeatStore::ensureSchema` in the same database, so backups cover them. Detail: [verifactu/README.md](modules/verifactu/README.md).

| Table | Content |
|-------|---------|
| `aeat_chain` | Chain head per `(environment, issuer_nif)`: `invoice_number`, `issue_date`, `hash`, `generated_at` |
| `aeat_records` | Every record generated: `environment`, `kind`, `issuer_nif`, `invoice_number`, `issue_date`, `total`, `hash`, exact `xml`, `generated_at`, `state`, `csv`, `error_code`, `error_description`, `attempts`, `last_attempt`, `reached`, `delayed` (1 once an incident kept it from being sent) |
| `aeat_chain_sync` | When each `(environment, issuer_nif)` chain was first synced with AEAT |
| `aeat_chain_anchors` | Huellas the chain was made to continue from although this database lacks their record (a sync adopted AEAT's head): the only such links `AeatChainCheck` accepts |
| `aeat_chain_restarts` | A chain started again as a new SIF, when a restored database lost a cancellation that may be the last record: `installation_number`, `restarted_at`, `previous_number`, `cancelled_invoice` |
| `aeat_installation` | One row: the records' `NumeroInstalacion`, which travels with the database |
| `aeat_environment_start` | One row per environment: when the direct client began there. For `produccion` nothing earlier is sent to the real AEAT |

### Shared helpers (`sql_lite.h`)

`parseDbDate` / `dbDateText` (the stored date format), `displayEuro` and `parseDecimal` (amount text), `ticketHeader` (one ticket's header row), `quarterOfMonth` / `quarterFirstDay` / `quarterLastDay` / `quarterKey`, `quarterIsClosed` / `closeQuarter` / `reopenQuarter` / `quarterClosures`, `genHash16`. The function reference is [sql_lite.md](modules/sql_lite.md).

## Verifactu

`VerifactuIntegration` talks to AEAT through one of two backends behind `VerifactuBackend`, chosen by `verifactu.connection`. The full reference is [verifactu/README.md](modules/verifactu/README.md).

| Endpoint | URL | Source |
|----------|-----|--------|
| Gateway | `https://facturae.irenesolutions.com:8050/Kivu/Taxes/Verifactu/Invoices` (test and production) | `verifactuconfig.cpp` |
| Direct, production | `https://www1.agenciatributaria.gob.es/wlpl/TIKE-CONT/ws/SistemaFacturacion/VerifactuSOAP` | `aeatdirectbackend.cpp` |
| Direct, pre-production | `https://prewww1.aeat.es/wlpl/TIKE-CONT/ws/SistemaFacturacion/VerifactuSOAP` | `aeatdirectbackend.cpp` |
| QR validation, production | `https://www2.agenciatributaria.gob.es/wlpl/TIKE-CONT/ValidarQR` | `aeathash.cpp` |
| QR validation, testing | `https://prewww2.aeat.es/wlpl/TIKE-CONT/ValidarQR` | `aeathash.cpp` |

The gateway `ServiceKey` is not in source control (obtain it from the gateway's site); the direct client needs no key, only the owner's certificate. `VerifactuResult::Status`: `SUCCESS`, `PENDING`, `ERROR`, `NETWORK_ERROR`, `INVALID_CONFIG`.

## Third-party dependencies

| Library | Purpose |
|---------|---------|
| Qt Widgets, Gui, Sql (SQLite driver), PrintSupport (`QPrinterInfo`), Network (gateway REST), Test | Framework, database, printer list, HTTP, tests |
| winspool | RAW print spooler (`printing`) |
| winhttp, crypt32, ncrypt | The direct client's mutual-TLS SOAP transport and certificate access; `crypt32` also for DPAPI in `appsettings` |
| qrcodegen (vendored, `src/third_party/qrcodegen/`) | Local QR encoder (`AeatQr`) |

## Tests

Testing is documented in [testing/](testing/README.md): running, conventions, adding a suite, the CI toolchain, every suite ([unit_suites.md](testing/unit_suites.md)), the end-to-end bench ([e2e.md](testing/e2e.md)), the UI smoke test ([ui_smoke.md](testing/ui_smoke.md)) and the manual checklist ([smoke_test.md](testing/smoke_test.md)). Open issues are in [progress_tracker.md](progress_tracker.md).
