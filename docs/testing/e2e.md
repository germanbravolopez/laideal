# End-to-end test bench

Real application objects working together (windows, Verifactu client, HTTP, DB) against a seeded throwaway database and a fake Verifactu gateway or a fake AEAT. Whole flows (save or pay a ticket, submission, reply, estado and QR, cancellation, rectification, recovery, Contabilidad) are checked in every CI build without reaching the real AEAT. Conventions shared with the other suites are in [README.md](README.md); the module-level suites are in [unit_suites.md](unit_suites.md).

| CTest | File | Drives | Fake server |
|-------|------|--------|-------------|
| `e2e_verifactu` (offscreen) | `test_e2e_verifactu.cpp` | `PayDialog` and the gateway client | `FakeVerifactuServer` |
| `e2e_app` (offscreen, 600 s timeout) | `test_e2e_app.cpp` | `MainWindow` and every app dialog at screen level, with both connections | both |
| `e2e_aeat_direct` (offscreen) | `test_e2e_aeat_direct.cpp` | The direct AEAT client with the real Cobrar window | `FakeAeatServer` |

## Safety rules

- **Endpoint**: the test-only seams `VerifactuConfig::setEndpointOverride(url)` (gateway) and `AeatDirectBackend::setEndpointOverride(url)` (direct) point every call at a fake server on `127.0.0.1`. Only code linked into a test can call them. The suites refuse to start unless the endpoint is local, and `LAIDEAL_TEST=1` (set by CTest) makes the transports refuse any other host.
- **Database**: a throwaway SQLite file in a `QTemporaryDir`, built and seeded by the test.
- **Settings**: a throwaway file through `AppSettings::loadFrom`, so `~/.laideal_settings.json` is never touched. Printing, the update check, the startup backup and the pending-recovery dialog are off; `test_e2e_app` also points the reports root at the temp dir and keeps generated PDFs from opening in a viewer (`ReportHtml::setOpenGeneratedReports(false)`).

---

## Building blocks - `tests/support/`

### `FakeVerifactuServer`

HTTP/1.1 server on `127.0.0.1` (random port) standing in for the IreneSolutions gateway.

- `start()`, `baseUrl()`: pass the URL to `VerifactuConfig::setEndpointOverride`.
- `requests()`, `requestsTo("Create")`: every request with its endpoint and parsed JSON body.
- `enqueue(endpoint, Reply)`: the next request to that endpoint gets a scripted `Reply` (`body`, `delayMs`, `drop` the connection, HTTP `status`, e.g. 401 for a refused key); later ones get the default.
- Defaults: Create and Cancel are accepted (incrementing CSV `A-FAKE0001`, a validation URL, a `Huella`, a real PNG QR); GetQrCode returns the PNG; GetFilteredList returns an empty list.
- Reply builders shaped like the captured fixtures: `acceptedReply`, `rejectedReply`, `queryReply`.

### `FakeAeatServer`

The AEAT SOAP service (VERI*FACTU) over plain HTTP on `127.0.0.1`. Like AEAT it registers every new record (Correcto with a CSV), answers a record it already holds with the duplicate reply (Incorrecto 3000, RegistroDuplicado), reports a wait time and answers queries from what it registered. Replies follow the official response schemas. The same class runs as a process in the UI smoke test (`tests/tools/fake_aeat_server.cpp`, [ui_smoke.md](ui_smoke.md)).

| Area | Members |
|------|---------|
| Start, inspect | `start()`, `url()`, `requests()`, `submissions()`, `sentRecords()` (issuer, operation, number, date, total, huella, previous huella, installation number, `RechazoPrevio`), `registered()` (records held, keyed `Alta:<number>`) |
| Scripted replies | `enqueue(Reply)` with `body`, `status`, `delayMs`, `drop`, `gated` (held until the test calls `releaseGated()`: no time window), `dropAfterRegistering` (register the records, then lose the answer); `faultReply(code, text)` |
| Submissions | `setWaitSeconds`, `rejectNext(number, code, description)` (once), `faultWhenSending(number)` (client SOAP fault for any submission carrying it), `omitLineFor(number)` (no line for that record in the reply), `dropNextSubmission()` (lost before registering) |
| Query content | `setQueryWithoutHashes` / `queryWithoutHashFor(number)`, `hideFromQuery(number)` (held but never returned), `setQueryWithoutSystemBlock`, `setQueryHashesLowerCase`, `setQueryByMonth` (only the asked month, as AEAT), `setQueryPageSize` (pagination), `setQueriesUnavailable` (queries fail with a server fault) |
| Cancelled invoices | By default a query about a cancelled invoice returns the invoice's data with the huella, time and link of its cancellation record (what AEAT's test environment does). `setQueryHidesCancellationRecord(true)`: the registration's values, the cancellation hidden (AEAT's written answer, the case that restarts a chain). `setQueryShowsCancellersSystem(true)`: the SistemaInformatico block of the cancellation instead of the registration's |

### Helpers

| File | Role |
|------|------|
| `modalautocloser.h` | Headless, nobody clicks "Aceptar". `ModalAutoCloser` closes any `QMessageBox` shortly after it opens, answers Yes to a Yes/No, and records title and text: a scenario asserts `sawMessageContaining("Trimestre bloqueado")` |
| `modaldriver.h` | Operates the app's own modal dialogs (Cobrar, the Verifactu dialog, the AEAT comparison dialog). `expect(ModalDriver::named("aeatReconcileDialog"), act)` or `ModalDriver::ofType<PayDialog>()` registers a step run once on the first matching visible window; it reads the dialog, presses buttons or closes it |
| `testschema.h` | `TestSchema::create`: the original pre-Verifactu `ingresos` columns, then the app's `migrateDatabase()`, so a migration's column reaches every suite. Used by the e2e suites, `test_sql_lite` and others |
| `e2efixture.h` | Namespace `E2e`: `createSchema`, `configureSettings` (synthetic issuer `B00000000`, production off, every external effect off), `clearTables`, `exec` / `scalar`, and the seeders `seedGarment` (unpaid, `SIN COBRAR`) and `seedSentGarment` (paid, `ENVIADA`, with a CSV) |

---

## `e2e_verifactu`: PayDialog and the gateway

Drives the real `PayDialog` as the Cobrar button does, with the real Verifactu integration, network stack and `sql_lite` writes.

| Group | Covers |
|-------|--------|
| Accepted payment | One Create with the bare ticket number and the service key; rows paid, `ENVIADA`, with CSV, huella and date; no pop-up |
| Partial payments | Each payment event is its own invoice (`200`, `200-1`), the second garment has seq 1 |
| Gateway failures | 500, 401, malformed JSON, dropped connection, success without CSV, a stalled reply (network error at the 30 s transfer timeout): rows stay paid and `PENDIENTE`, never `ERROR`, since the outcome is unknown |
| Late reply | A reply after PayDialog's 5 s wait: payment kept `PENDIENTE`, the retry answered "already exists", the query adopts the matching record as `ENVIADA` |
| AEAT rejection | The sale stays paid with estado `ERROR` and the reason |
| Refusals | A payment in a closed quarter; a rectificativa through the gateway in production; a missing or refused service key reported as such (*Probar conexión*) |
| In-flight state | Re-initialising the integration keeps a reply in flight; a rectification counts as in flight until its reply; an HTTP error body's message is kept |
| Closed quarter | Pay in Q1, close Q1, cancel in Q2: the paid garment is `ANULADA` with `fecha_anulacion`, Q1 figures unchanged, the unpaid garment still chargeable and invoiced as `600-1`, Q2 nets the regularisation |

## `e2e_app`: windows at screen level

The application's windows live in the `laideal_app` static library (the executable is only `main.cpp` and resources), so the suite fills the widgets (found by object name), presses the button or invokes the slot it is wired to, and asserts on the DB, on what reached the fake server and on the result panels. `MainWindow` opens `DB_PATH` (set with `setDbPath`), so the test reads the same file through its own connection. Dialogs the screens open with `exec()` are operated by `ModalDriver`. Most scenarios use the gateway; the direct-connection ones use `FakeAeatServer`.

| Group | Covers |
|-------|--------|
| Save a ticket (MainWindow) | Unpaid and paid saves, price lookup, new client stored once, form reset and next number; refusals (no client, unpriced or negative quantity, unmeasured m2 paid, closed quarter, failed insert is all-or-none); keys and listings keeping what is being typed; the print-after-submit wait and a reset during it |
| Start-up | The post-update check asked once (also from 12.0 and with backups off); pre-migration copy; start-up recovery of `PENDIENTE` payments (Reintentar, Posponer, Marcar como error, scoped by seq, escaped text); recovery waiting for an open dialog; the start problems kept with a Verifactu warning; the status bar's environment and unsent-record count |
| Cobrar (PayDialog) | Stores the amount it sends and the payment before submitting; refuses zero, an unmeasured m2 garment, a garment paid meanwhile, no integration, an unreadable invoice number; a second press sends nothing; payment dates bounded by reception and today; escaped client name |
| Recogida | Search modes (by date, name, phone, beyond the first 256 rows), selection and totals in cents, manual price and m2 size, split of a paid garment staying in its invoice, refused writes reported, void and add-garment from the window, reprint rules, the late reply or rejection after PayDialog gave up, the Verifactu dialog (duplicate retry and AEAT comparison adopting the CSV only when the data match, *Consultar*), a reply for an already settled invoice ignored |
| Anular prendas | Voids only the ticked garments locally; refuses a paid, sent or handed-over one; no AEAT request |
| Anular factura | Confirmation first, each payment event cancelled with its own date, older data and unreadable dates, refused while the quarter is closed or a rectificativa stands, the reply applied to the ticket it was sent for, dialogs not closable while AEAT answers, a queued direct cancellation is pending not an error |
| Rectificar factura | Substitution date rules and by-differences limits, R5 naming the stored record's invoice, the direct client's R5-only rule and queued answers, pending / refused / orphaned rectificativas (redone under their own number, in a closed quarter, with an unreadable date or comma amount), no second send while one is pending |
| Añadir nuevas prendas | A paid garment submitted at once, a retyped ticket number refused, free-typed garments refused, payment before production refused |
| Imprimir and reprints | Recibo, factura completa and partial-payment invoices; the ticket bytes through `Imprimir::ticketBytes()` (DNI, address, leyenda, QR size); a reprint never blank, merged or reusing a previous QR, and drawn from the stored verification URL; the first-copy failure |
| Direct connection | Save and Cobrar print the factura with the QR before AEAT answers, rows `PENDIENTE` until it does; outcomes nobody waited for settled at start; a rejection after the customer left shown in the panel; one backend after Configuración; the status bar's environment |
| Chain gate and numbering | No invoicing until the chain is checked or while Verifactu is not configured; the number refreshed after any sync, never moved during a save; numbers above AEAT's after a restore; a chain restart announced; pre-production numbers following the database; the self-test sharing the shop chain only in pre-production |
| Export | Exportar registros AEAT: one record per payment event, cancellations, the XML for awkward stored data, the dialog's range, overwrite, empty period and write failure |
| Facturas and listings | Supplier-invoice form (save and refusals), Listado de gastos / clientes / prendas / ingresos (edit checks, closed-quarter locks, accent-insensitive search, PDF and its filter note), Actualizar |
| Contabilidad | Generate and lock a quarter, Comprobar bloqueo, detail report, Revertir; an unwritable report does not lock; unreadable data neither reports nor closes; an unfinished quarter is never closed; a quarter whose only movement is a cancellation |
| Menus and dialogs | The menu bar and shortcuts; Enter in number fields runs one action; arrow keys select the ticket |

## `e2e_aeat_direct`: the direct client

`VerifactuIntegration` initialised with the direct AEAT connection, the real Cobrar window and `FakeAeatServer`.

| Scenario | Covers |
|----------|--------|
| Payment registered | One record (invoice id, payment date, total); both rows `ENVIADA` with AEAT's CSV, the record's huella, its XML and the pre-production QR URL |
| Partial payment | The second event is `200-1`, chained after the first record |
| Production on purpose | The gateway's PRODUCCIÓN box alone leaves the direct client in pre-production; production needs its own setting |
| Rejection | The row is `ERROR` with AEAT's description |
| Reprint without a stored URL | Printed without QR: the direct client never rebuilds the URL for today's environment |

## Adding a scenario

1. Seed rows with `seedGarment` / `seedSentGarment` (or `exec()`).
2. Script the fake server (`enqueue`, `rejectNext`, ...) when the default "accepted" is not what you need.
3. Drive the flow through the dialog or window, the way an operator does.
4. Assert on the DB (`scalar`), on the fake's requests and on the result panel or `ModalAutoCloser` messages.
5. Keep delays short, and prove the scenario fails against a build with the guarded line broken.

## Not in this bench

Menu wiring, the printer queue and the installed app are covered by the UI smoke test ([ui_smoke.md](ui_smoke.md)); the physical printer, the shop's real schema and how screens look by [smoke_test.md](smoke_test.md). The real AEAT is checked by the pre-production script and once with a real ticket after the production switch.
