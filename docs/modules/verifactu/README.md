# Verifactu Module (`src/verifactu/`)

Spanish mandatory digital invoicing (AEAT). Every invoice is sent to AEAT in real time; the CSV security code and the QR are kept with the ticket. Two connections implement it:

- **IreneSolutions gateway**: a REST service in front of AEAT (the default; API in [rest_api.md](./rest_api.md)).
- **Direct AEAT client**: SOAP to AEAT with the owner's certificate (the shop's connection, §Direct AEAT connection).

The legal requirements and how the code meets them: [verifactu-requirements.md](./verifactu-requirements.md). Research log of the direct client: [aeat-direct-investigation.md](./aeat-direct-investigation.md). Status and open work: `docs/progress_tracker.md`.

## Source files

| File | Purpose |
|------|---------|
| `verifactuintegration.*` | Facade: the only class `MainWindow` and the dialogs call. Chain gate, `splitGross` of the amounts, the "before production" rule |
| `verifactubackend.h` | `VerifactuBackend`: the interface both connections implement (submit, cancel, QR, query, `recordSettled`) |
| `verifactumanager.*` | Gateway: REST over `QNetworkAccessManager`. `testConnection` tells a refused key (401 / 403 or a key error in the body) from a network failure |
| `verifacturesponse.*` | Pure `parseVerifactuResponse()` (JSON -> `VerifactuResult`), `parseVerifactuQueryResponse()`, `decodeVerifactuImageBase64()` |
| `verifactuconfig.*`, `verifactuinvoice.*`, `verifactutaxitem.*` | Gateway config holder and JSON model (`TaxType` always `S1`) |
| `verifactutypes.h` | `VerifactuEstado` + string helpers, `VerifactuResult`, `splitGross` |
| `testhostguard.h` | With `LAIDEAL_TEST=1` (every CTest test) both transports refuse any host but this machine |
| `aeathash.*` | Huella of alta / anulación records, amount and timestamp formats, QR verification URL |
| `aeatrecord.*` | `RegistroAlta` / `RegistroAnulacion` XML, submission and query SOAP envelopes, `laIdealSystem` |
| `aeatresponse.*` | AEAT replies and SOAP faults -> `VerifactuResult` / `VerifactuRemoteRecord`; `summarizeRecord`, `chainTip`, `carriesCancellationRecord` |
| `aeatstore.*` | The `aeat_*` tables (below) |
| `aeatchaincheck.*` | `AeatChainCheck::verify` / `verifyTail`: recomputes every stored huella and checks the links (requirement 1) |
| `aeatqr.*` | The QR drawn locally (vendored Nayuki qrcodegen), always level M |
| `aeatcertificate.*` | The owner's certificate: Windows personal store (thumbprint) or a .pfx imported in memory |
| `aeattransport.*` | SOAP POST through WinHTTP presenting the certificate (Qt's Schannel backend cannot read PKCS#12) |
| `aeatdirectbackend.*` | Builds, chains, stores and sends the records; `syncChain`, `adoptTip`, `restartChain`, `settleRecord` |
| `aeatselftest.*` | *Ensayo en el entorno de pruebas de la AEAT* (Configuración): registers, queries and cancels a test invoice in AEAT pre-production |
| `aeatconnectionprobe.*` | *Probar conexión* of the direct connection (below) |
| `producerdeclaration.*` | The declaración responsable (art. 15 of the Orden), see requirement 8 |

## Architecture

```
MainWindow / RecogPrendas / PayDialog / CancelInvoiceDialog / RectifyInvoiceDialog / Imprimir
        │
        ▼
VerifactuIntegration (facade: chain gate, splitGross, before-production rule)
        │  both implement VerifactuBackend
        ├── gateway: VerifactuManager ──▶ VerifactuConfig, VerifactuInvoice + TaxItem (JSON)
        └── direct:  AeatDirectBackend
                ├──▶ AeatRecord / AeatHash / AeatQr    (records, huella, QR)
                ├──▶ AeatStore                         (aeat_* tables)
                ├──▶ AeatTransport + AeatCertificate   (WinHTTP, mutual TLS)
                └──▶ AeatChainCheck                    (stored-chain verifier)
```

- Both backends answer on the same `requestFinished` / `queryFinished` signals, so callers do not change. `SettingsDialog::testConnection` uses a transient `VerifactuManager`.
- Calls are async: they return a request id and the result arrives on `requestFinished`.
- `splitGross` splits every invoice amount: base rounded to cents, cuota the rest, so base + cuota is exactly the amount charged. The printed factura shows the same split.

## Public API (`VerifactuIntegration`)

```cpp
m_verifactuIntegration->initialize();     // false if not configured: invoices are refused (invoicingBlockedReason)
connect(m_verifactuIntegration, &VerifactuIntegration::requestFinished, this, &MyClass::onFinished);

QString id = m_verifactuIntegration->submitSimplifiedInvoiceAsync(ticketNumber, ticketDate, taxBase, ivaRate, text);   // F2
id = m_verifactuIntegration->cancelInvoiceAsync(invoiceNumber, invoiceDate);
id = m_verifactuIntegration->submitRectificationAsync(newNumber, date, RECTIFICATION_R1, BY_DIFFERENCES,
         newTaxBase, newTaxAmount, origTaxBase, origTaxAmount, ivaRate, text);   // BY_SUBSTITUTION: new* = corrected total, orig* = replaced values
id = m_verifactuIntegration->generateQRAsync(invoiceNumber, invoiceDate, taxBase, ivaRate, text);   // gateway reprint only
```

- An empty id means the call was refused synchronously (not configured, a non-rectificativa type, the chain gate, a date before production); the reason is in `getLastError()` and no signal fires. Otherwise `requestFinished` fires exactly once, also for validation errors (queued).
- `VerifactuResult`: `status` (`SUCCESS`, `PENDING`, `ERROR`, `NETWORK_ERROR`, `INVALID_CONFIG`), `csv`, `errorCode`, `errorDescription`, `validationUrl`, `qrCode`.

### `VerifactuEstado` (`ingresos.verifactu_estado`)

| Enum | DB string | Meaning |
|------|-----------|---------|
| `Unpaid` | `SIN COBRAR` | No invoice yet. Written when `pagado != "SI"`; becomes `PENDIENTE` only when a submit is due |
| `NotSubmitted` | `PENDIENTE` | Paid and due at AEAT: sent and awaiting a reply, or not sent yet. NULL / empty (legacy rows) maps here |
| `Enviada` | `ENVIADA` | Accepted by AEAT |
| `Anulada` | `ANULADA` | Cancelled (`CancelInvoiceDialog`: only the paid rows of that payment event; unpaid garments stay chargeable), or a locally voided garment. Gets `fecha_anulacion` |
| `Rectificada` | `RECTIFICADA` | Superseded by a substitution (`S`) rectificativa (paid rows only). Gets `fecha_anulacion` = the rectificativa's date; a row without it is excluded from income (`kIngresosIncomeWhere`) |
| `Error` | `ERROR` | AEAT definitively refused the submission or cancellation |
| `Unknown` | any other text | Written by no version (an edit outside the app): logged, never resent, cancelled, rectified or voided, printed without QR or leyenda |

Rules:
- Never hardcode the strings; use the helpers. Map an AEAT reply with `verifactuEstadoForResult(status)`: only a definitive refusal (`ERROR`) becomes `Error`. A timeout or transport failure leaves the outcome unknown (AEAT may hold the invoice), so it stays `PENDIENTE` and goes to the start-up recovery dialog. Recording `Error` would offer a "Reintentar" that can only answer "already exists".
- `Unpaid` and `NotSubmitted` differ only in the start-up recovery dialog ("nothing to send" vs "sent, reply lost"). Every other gate uses `verifactuEstadoIsUnsubmitted()` (true for `Unpaid`, `NotSubmitted`, blank): paying a garment does not rewrite the estado, so a row is still `SIN COBRAR` when `RecogPrendas` decides to submit it (callers: `garmentIsLocallyVoidable`, the `PAY_YES` trigger).
- `migrateDatabase()` repairs, each idempotent: unpaid `'PENDIENTE'` rows are relabelled `'SIN COBRAR'` (NULL / `''` stay: they are legacy payments, left out of Imprimir's event list and Anular by `verifactu_estado != ''`); unpaid garments an old cancellation wrongly marked `ANULADA` go back to `'SIN COBRAR'` (unpaid `RECTIFICADA` rows are only logged: an old substitution may include their amounts); a paid row split off with blank `verifactu_*` columns copies them from the one `ENVIADA` invoice of its ticket with the same `fecha_pago`, `prenda` and `servicio` (ambiguous matches are only logged; amounts never change). `splitGarmentRow` copies the whole row, so a split stays in its invoice.

### Waits and in-flight requests

- Gateway: every request has a 30 s transfer timeout (*probar conexión* 10 s); the UI waits 3 s (MainWindow save) or 5 s (PayDialog). The UI gives up first by design, so a request is often still in flight after the ticket printed. Do not raise the waits without weighing the counter freeze.
- The reply parser (`parseVerifactuResponse`, told whether it answers a submit, cancel or QR) treats a body that is not JSON or has no `ResultCode` as `NETWORK_ERROR` (unknown outcome), never as a refusal; a submit "success" without AEAT's CSV is `PENDING`, never `ENVIADA`.
- When the wait expires: the printed document degrades to a recibo with IMPORTE PAGADO and no QR (a recibo is a claim ticket, never a tax document); the request is adopted by an object that outlives the print (`RecogPrendas` for PayDialog via `submitAdopted`; MainWindow keeps `m_pendingSubmits`), so a late reply still patches the row, and a late success tells the operator the factura can now be printed.
- Direct client: no wait. Once the record is generated and queued, the QR comes from the stored record (`qrBeforeReply`) and the factura prints at once with the leyenda; the rows stay `PENDIENTE` and the answer is adopted (`submitAdopted(..., facturaPrinted = true)`, so a late success asks for no reprint). AEAT's `TiempoEsperaEnvio` (60 s) would otherwise stall every second ticket.
- A definitive refusal shows in the result panel. The status bar counts refused invoices until they are resent (`MainWindow::updateRefusedInvoices`); the count opens *Facturas rechazadas* (`RefusedInvoicesDialog`, `sql_lite::refusedInvoices`: ticket, payment date, amount, AEAT's reason), whose *Abrir en Recogida* selects the refused row (`RecogPrendas::showTicket`) for Verifactu… → Reintentar / Consultar en AEAT.
- Floor: invoices whose earliest **payment date** is before `verifactu.pending_recovery_floor_date` (*Solo tickets desde*) are left out of the count and the list; the start-up pending check compares the **reception date**. An unreadable floor or payment date never hides a refusal. When production began later than the floor, that day is the floor (`verifactuRecoveryFloorIso`).

## Reconciling with AEAT (Consultar en AEAT)

When AEAT registered an invoice whose reply was lost, every retry answers "ya existe" and the row sticks at `ERROR`. A query of what AEAT holds resolves it (gateway: `/GetFilteredList`, see [rest_api.md](./rest_api.md); direct client: the SOAP query). Entry points: **Consultar en AEAT** in the Verifactu detail dialog (any paid row), and an automatic query when `verifactuErrorIsDuplicate()` recognises a duplicate rejection.

Rules that keep an unconfirmed reply from causing a false regulatory claim:
1. **Ignorance is not absence.** A failed query or unreadable body is `parsed=false`, never "AEAT does not have it". Only a well-formed empty result means absent.
2. **Identity must be proven.** `verifactuRemoteMatches()` needs InvoiceID, date and amount to agree to the cent; a missing date or zero amount is missing evidence.
3. **The operator confirms.** The dialog shows AEAT's record beside the local one (raw JSON one click away) and enables the write only when the match holds and a CSV came back.

The write is `sql_lite::reconcileVerifactuFromAeat()`: it refuses an empty CSV and never touches an `ENVIADA` / `ANULADA` / `RECTIFICADA` row; it adopts only the CSV (no payload, so such an invoice exports with `sinPayload="1"`).

## Direct AEAT connection

Selected in Configuración → Verifactu → *Conexión con la AEAT* → *Directa con la AEAT* (`verifactu.connection = aeat`). The `ingresos.verifactu_*` columns, the estados and every caller are the same as with the gateway (`verifactu_xml` / `verifactu_hash` = the record the app built).

### Tables (`AeatStore::ensureSchema`, in the shop DB, covered by the backups)

| Table | Content |
|---|---|
| `aeat_chain` | Head of each issuer's chain per environment (`pruebas` / `produccion`): invoice, date, hash, generation time |
| `aeat_chain_sync` | When each issuer's chain was first synced with AEAT; until then no record is generated |
| `aeat_chain_anchors` | Huellas the chain was made to continue from although this database lacks their record (a sync adopted AEAT's head): the only foreign links `AeatChainCheck` accepts |
| `aeat_chain_restarts` | One row per restart of a chain as a new SIF (`environment`, `issuer_nif`, `installation_number`, `restarted_at` UTC, `previous_number`, `cancelled_invoice`): earlier records are no longer links, and the check accepts that number's `PrimerRegistro` |
| `aeat_installation` | One row: the `NumeroInstalacion` every record carries, whatever the environment or issuer |
| `aeat_environment_start` | The day each environment's client began on this database (`AeatStore::markStarted`) |
| `aeat_records` | Every generated record: kind, invoice, total, hash, exact XML, state (`PENDIENTE`, `ACEPTADO`, `RECHAZADO`, `DUPLICADO` = AEAT holds another system's record, `ANULADA_EN_AEAT`), CSV, error, attempts, `reached`, `delayed` |

`aeat_records.reached`: a send that may have reached AEAT (a send cut off by closing the app counts; a batch AEAT refuses whole with a SOAP fault does not). `delayed`: the record an incident held back.

### The chain

- **One chain per system and issuer** (AEAT, 13-10-2026). It continues only records La Ideal generated, never the gateway's (which keep their own chain), and starts with `PrimerRegistro` when there are none. An unreadable chain is an error, never `PrimerRegistro`. Every record names La Ideal as producer (`AeatRecord::laIdealSystem`), never the issuer.
- **Sync at each start** (`MainWindow::continueDirectChain` -> `AeatDirectBackend::syncChain`): asks AEAT for each record's `SistemaInformatico` and keeps only ours (system id `LI`, producer NIF). A record AEAT returns without its `SistemaInformatico` block or huella is unusable; a first sync then fails rather than restart the chain over our own records.
  - Daily start: this month and the previous one.
  - **First sync** (first start, restored database, new PC: the chain marker differs): the last 24 months, because the newest record may carry an older invoice date (a resend, a back-dated rectificativa). It must reach AEAT; later syncs fall back to the local chain when AEAT cannot be reached.
  - No record is generated before the first sync, and requests wait for it.
- **Numbering after a restore**: in production the next ticket number is above the issuer's highest at AEAT from any system (the gateway's too); in pre-production it always follows the database.
- **Chain marker** (`verifactu.aeat_chain_marker`, written the moment it changes by `AppSettings::setAeatChainMarker`): `<environment>|<issuer>|<huella of the last record>` as this installation last saw it. It tells whether the chain's last record is still visible (same PC).
- **`NumeroInstalacion`** lives in `aeat_installation` (it belongs to the chain, so a database restored on a new PC keeps it). Created on first use from the newest stored record's number, else new. A sync that finds the chain's last record at AEAT under another number takes that one (`adoptTip`). Without a stored number the backend generates nothing.

#### When does the chain continue, and when does it restart?

AEAT's query returns a cancelled invoice with its **cancellation record** (huella, time, chain link; `AeatResponse::carriesCancellationRecord` recomputes the cancellation huella from the reply to prove it; seen on the test environment 25-11-2026, production not yet). `adoptTip` (`AeatDirectBackend`) gathers the inputs and applies the result; the rules are `AeatChainSync::plan` (`aeatchainsync.h`, pure, tested by `aeat_chain_sync`). Per sync:

| Situation | Result |
|---|---|
| A returned cancellation is proven ours: this database holds its huella, the chain marker names it, or it chains to a record of ours (also under another system's block) | The chain continues from it |
| Unproven cancellation, or AEAT hides it, and it may be the chain's last record | The chain restarts as a new SIF (below), only on a first sync and never from the self-test (`adoptInstallationNumber` off unless it works on the shop's own chain, `selfTestSharesShopChain`) |
| Another system's registration | Never a link |
| No records of ours | `PrimerRegistro` |

Whether a hidden / unproven record "may be the last": with a marker that names a visible record, only if records of ours come after it in AEAT's times (the settings are older than the database); marker names none, or none exists: AEAT's own generation times decide.

**Restart** (`restartChain`; AEAT, 24-11-2026, art. 7.b of the Orden): a new `NumeroInstalacion`, `PrimerRegistro = S`, a row in `aeat_chain_restarts`, a notice in the main window. Unsent records of the old chain go first (the restart waits, repeats by itself, and its message names those invoices and AEAT's last error). Records before the restart are not candidates for the new chain (an unreadable generation time is never a link).

### Sending and outcomes

What happens when AEAT does not answer, or answers late:

- A submit or cancel stores the record at once and answers when AEAT replies. Requests resolve and connect within 10 s on a private thread pool, so closing the app never waits on AEAT; a waiting request is always answered (`releaseWaiters` before Configuración replaces the backend).
- Records wait for `TiempoEsperaEnvio` and go together. Records left unsent go at the next start; after a failed send they retry on their own. A SOAP fault on a whole submission refuses nothing: its records stay pending and go one by one, so only the faulty one is refused (AEAT question GZ44 item 1, interim).
- From an incident (no answer, an unusable certificate, records left by an earlier session) until nothing is pending, the status bar says how many records are still to send (`unsentRecordsChanged`, `lblUnsentRecords`; art. 16.4 of the Orden). The record the incident delayed is marked `delayed`; it, or one left by an earlier session, makes the submission say `Incidencia = S` (a send that merely started, or a backend replaced in the same run, does not).
- An unusable certificate stops the sending, not the records. Under 30 days to expiry the start shows a warning (`expiryWarning`); one that expires while the app is open stops sending with that reason.

How an answer settles a row:

- `recordSettled` -> `sql_lite::applySettledVerifactuResult`, also when a dialog waited (it may have closed). A settled row is never rewritten. `AeatStore::markOutcome` writes only a `PENDIENTE` record row; `AeatDirectBackend::settleRecord` keeps the stored answer otherwise and answers waiters with it; when the write itself failed the record stays pending, is sent again and the waiters get that reason. An accepted substitution also marks its original `RECTIFICADA`.
- A retry of the same invoice resends the stored record unchanged; an accepted one answers from the store.
- A **duplicate** reply is checked against AEAT's own record first (`verifyDuplicate`): adopted as accepted only when an earlier send reached AEAT and AEAT's amount and huella are ours. Another amount is an `ERROR` (another invoice under our number: Consultar en AEAT, then a new number or a rectificativa). An unreadable record stays pending; one AEAT calls a duplicate but never shows in 5 rounds becomes an `ERROR` to check. A duplicate of another system's record is accepted without claiming our XML; a duplicate of a cancelled invoice is reported and never registered again.
- A cancellation whose reply was lost is resent as the same record; AEAT answering that it holds it (invoice *Anulada*) is acceptance, never an error (`onSubmissionReply`).
- A rectificativa without an answer is `PENDIENTE` only while its record is pending (`holdsPendingRecord`). At start it is settled from AEAT's stored answer (`storedOutcome`) or becomes an `ERROR` to redo. While `PENDIENTE` its ticket cannot be rectified or cancelled. A refused one is redone in Rectificar factura under its own number (never while its record is pending; counted in a quarter closed since: only as filed, same date, amount and S/I mode). It is never resent as a plain invoice (Recogida's Reintentar and the start-up recovery skip it). With the gateway, one whose dialog closed before the answer becomes an `ERROR` to redo.

### The chain gate

While the first chain check runs or has failed, no ticket is saved, charged, extended with paid garments or rectified (`invoicingBlockedReason`, `numberingBlockedReason`). The facade enforces it: `submitSimplifiedInvoiceAsync`, `cancelInvoiceAsync` and `submitRectificationAsync` refuse (empty id, reason in `getLastError`), whichever window asks. Anular shows the reason; the start-up *Reintentar* (`PendingSubmitsDialog::retry`) keeps the row listed with it. A failed check is retried; every finished check (`chainChecked`) moves the number on screen past AEAT's highest.

### Tests of the connection

- **Probar conexión** (`AeatConnectionProbe`): loads the chosen certificate (refuses an expired one, warns when its NIF is not the issuer's) and asks AEAT for the issuer's invoices of the current month in the environment ticked in the form. A query only: no record, no database, so it runs any time.
- **Ensayo en el entorno de pruebas de la AEAT** (`AeatSelfTest`): registers `PRUEBA-<date time>` of 1,21 EUR, queries it back and cancels it, waiting for each record's final outcome (retries included). Hidden while the saved settings are direct + pre-production (the tickets are the test); with the gateway or production it stays (rehearsal for a new certificate or version).

### Going to production

The shop registers in AEAT pre-production until Configuración → Verifactu → *Enviar a la AEAT real (PRODUCCIÓN)* is ticked and accepted. No release is needed: the app handles what came before.

- **The switch checks** (`SettingsDialog::confirmConnectionChange`, run by `accept`): refused while test-environment records are unsent; refused when the production check fails (`AeatConnectionProbe`: the certificate and one read-only query to the real AEAT); then a confirmation. Leaving production is refused while production records are unsent, then confirmed; so is leaving the direct connection with records unsent, and the gateway's PRODUCCIÓN box. The production chain is new: `PrimerRegistro` after a first sync of 24 months.
- **The day production began** is kept in `aeat_environment_start` (`AeatStore::markStarted`): set when the owner confirms the switch (again if production is left and entered anew) or the first time a production client runs. The status bar shows it (*AEAT: PRODUCCIÓN desde el <día>*); in pre-production it says "AEAT: entorno de PRUEBAS" (`lblAeatEnvironment`).
- **One rule for "before production"**: an invoice dated before that day was never held by the real AEAT.
  - `VerifactuIntegration::dateBeforeProductionReason` refuses a new invoice with such a date: the payment date in Recogida starts at that day, and a paid ticket or paid added garment dated earlier is refused before it is stored (MainWindow, PayDialog, AddGarment).
  - `submitSimplifiedInvoiceAsync` (and rectificativas) refuse to send one: a retry of an old ticket is refused.
  - Pending / refused lists use that day as floor when later than *Solo tickets desde*.
- **Anular of an invoice the real AEAT never held** (`invoicePredatesProduction`: no production registration of it, and dated before that day or, on the day itself, accepted by the test environment): `CancelInvoiceDialog::cancelLocally` cancels it in the books only. Nothing is sent, a warning goes to the log, `verifactu_cancel_xml` holds an `<AnulacionLocal>` note and the AEAT export writes `local="1"` on its `<Anulacion>`. `cancelInvoiceAsync` refuses to send one.
- **Left as they are**: a rectificativa issued in production that corrects an earlier ticket is sent (a new invoice; AEAT's answer to the reference is untested); a reprint keeps the QR the invoice was issued with (test host for earlier ones); the export marks every record's `entorno` from its QR host.

## Configuration

Source of truth: `~/.laideal_settings.json` (`AppSettings`); edit in Archivo → Configuración → Verifactu.

| JSON key | Content |
|----------|---------|
| `verifactu.nif` / `verifactu.company_name` | Issuer NIF and name (`CompanyName` sent to AEAT) |
| `verifactu.service_key` | Gateway ServiceKey, DPAPI-encrypted (`dpapi:v1:`); obtain at https://facturae.irenesolutions.com/verifactu/go |
| `verifactu.environment` | Gateway: `"TESTING"` (default) or `"PRODUCTION"`. The ServiceKey decides where invoices go (one endpoint); the box only changes the unused validation URL and makes the app refuse gateway rectificativas in PRODUCTION |
| `verifactu.connection` | `irenesolutions` (default) or `aeat` |
| `verifactu.aeat_direct_production` | Direct client environment: `false` (default) = pre-production. Set only by *Enviar a la AEAT real (PRODUCCIÓN)* (with confirmation), never by the gateway's box |
| `verifactu.aeat_chain_marker` | Chain marker (above). A database whose chain differs must reach AEAT at start before any record |
| `verifactu.aeat_certificate_thumbprint` | The owner's certificate in the Windows personal store (SHA-1 thumbprint) |
| `verifactu.aeat_certificate_file` / `aeat_certificate_password` | Else a .pfx and its DPAPI-encrypted password |
| `verifactu.pending_recovery_enabled` | `true` (default): the start-up dialog lists `PENDIENTE` rows; only an explicit `false` turns it off |
| `verifactu.pending_recovery_floor_date` | ISO date, default `2026-09-01` (`AppSettings::defaultPendingRecoveryFloorDate()`); see Waits. Never move it past a real production invoice |

`initialize()` reads these (`loadEmitterConfiguration`) and returns `false` with a `qCritical` when NIF or name is empty (or the gateway has no ServiceKey: "Clave de servicio no configurada"). The app stays usable, but a paid ticket is refused while Verifactu is not configured (`invoicingBlockedReason`); unpaid tickets still save.

## DB persistence (`ingresos`)

Columns added by `migrateDatabase()` (idempotent `ALTER TABLE ADD COLUMN`):

| Column | Content |
|--------|---------|
| `verifactu_csv` | AEAT security code; empty if not submitted |
| `verifactu_timestamp` | ISO-8601 submission time |
| `verifactu_estado` | `VerifactuEstado` |
| `verifactu_error` | Error text when `ERROR` |
| `verifactu_url_qr` | Validation URL the QR encodes |
| `verifactu_xml` | The record AEAT registered (gateway: `Return.Xml`); empty if not submitted or recovered with Consultar en AEAT. Source of the AEAT export (art. 14.1) |
| `verifactu_cancel_xml` | AEAT's cancellation record stored on the cancelled event's paid rows, or the `<AnulacionLocal>` note; exported as `<Anulacion>` |
| `verifactu_hash` | 64-char hex huella from `verifactu_xml` (art. 12) |
| `verifactu_rectifies_n_recibo` | On a rectificativa row, the `n_recibo` of the corrected ticket |
| `verifactu_rectification_type` | `S` or `I` on a rectificativa row |
| `verifactu_invoice_seq` | Payment event of the ticket (see below) |

`kIngresosIncomeWhere` (`sql_lite.cpp`) excludes `ANULADA` and `RECTIFICADA` rows from quarterly income; every other estado counts. Exception: a row cancelled or rectified with `fecha_anulacion` stays income in its payment period and is subtracted in the period of that date (`docs/modules/contabilidad.md`).

One invoice per payment event `(n_recibo, verifactu_invoice_seq)`: `InvoiceID` is `<n_recibo>` for seq 0 and `<n_recibo>-<seq>` after. A ticket's first payment event and its unpaid remainder both carry seq 0; every statement is scoped by `pagado = 'SI'`. `nextVerifactuInvoiceSeq` fails closed (an unreadable `ingresos` or `aeat_records` read means no charge).

## Integration points

### Guardar ticket (`MainWindow::on_pb_save_clicked`)
- Stores the rows in one transaction (`sql_lite::insertGarmentRows`, all or none) as `PENDIENTE` when paid, `SIN COBRAR` when not; if they cannot be stored nothing is kept, sent or printed.
- A paid ticket submits the stored total and prints the factura with QR for the client and the recibo with IMPORTE PAGADO for the shop: at once with the direct client, else after waiting up to 3 s (no reply: both recibos, no QR). An async handler writes the reply into the rows.
- Refused while the chain gate blocks, or Verifactu is not configured (paid tickets only), or the payment date is before production.

### Añadir prendas (`AddGarment` -> `paidGarmentSaved` -> `VerifactuCoordinator::submitInvoice`)
A garment added as paid (Tickets → Añadir nuevas prendas, or Añadir in Recogida) is the ticket's first invoice (tickets with a paid garment are refused, re-checked at save), submitted at once as seq 0 with its payment date. No bounded wait: the row stays `PENDIENTE` for the start-up recovery without a reply. Neither window opens while the chain gate blocks.

### Cobrar (`PayDialog::onCobrarClicked`)
- Stores the selected garments first (`storeChargedGarments`, all or none: paid + `PENDIENTE`, their seq, `InvoiceID` `<n>-<seq>`), then submits. A crash during the round trip leaves a payment the recovery resends, never an invoice at AEAT for an unpaid ticket. The statement refuses voided rows and unreadable amounts, a garment paid meanwhile, and a closed quarter.
- Refused before anything is stored: a 0,00 total, no integration, an unmeasured m2 garment, a payment date in a closed quarter or before production, an unreadable seq, the chain gate.
- Prints one factura with QR (direct: at once, reply adopted by `RecogPrendas`; gateway: after 5 s). `SUCCESS` -> `ENVIADA` + CSV / QR; AEAT `ERROR` -> `Error` (`updateTicketVerifactuFields`, scoped by seq and `pagado = 'SI'`); timeout, transport failure or `INVALID_CONFIG` -> rows stay `PENDIENTE`. No request id: the payment stays `PENDIENTE`, a recibo prints, Recogida shows the reason (`notSentReason`).
- Esc, the window X and Cancelar are refused while AEAT answers (`PayDialog::done`); the dialog closes on the reply or after the wait, emitting `submitAdopted`. Anular and Rectificar refuse closing the same way.

### Verifactu button (`RecogPrendas::on_pb_verifactu_clicked`)
Shows estado, CSV, timestamp, error and the clickable validation URL. On `ERROR`: "Reintentar envío a AEAT" (`retryVerifactuSubmit(ticket, seq)`). On any paid row: "Consultar en AEAT".

### Start-up recovery (`PendingSubmitsDialog`)
- Lists events left `PENDIENTE` / NULL / empty (`sql_lite::pendingVerifactuEvents`, one per `(n_recibo, seq)`), gated by `verifactu.pending_recovery_enabled`, the floor date and **paid** (`pagado = 'SI'` with a `fecha_pago`).
- Never lists or resends an invoice still in flight (`VerifactuIntegration::isInFlight`).
- Reintentar re-submits that event's own `SUM(importe)` under `verifactuInvoiceId(n_recibo, seq)`; Marcar error scopes by seq; Posponer. A slow-but-registered submission is caught by the duplicate rejection.

### Anular (`CancelInvoiceDialog`, Facturas → Anular factura Verifactu…)
- Two presses: the first says what will be cancelled (invoice, date, amount), the button becomes *Confirmar anulación*.
- One line per payment event over **paid** rows. One request per event, with the event's InvoiceID and its **payment date** (earliest `fecha_pago` of its paid rows, `sql_lite::submittedInvoiceEvents`; an event without a readable date is refused). A rejected seq-0 invoice whose reception date differs is retried once with the reception date.
- Refused while an accepted rectificativa stands on the ticket, through the chain (`liveRectificationOf`: a rectificativa itself substituted counts as the later one), and while today's quarter is closed (`quarterIsClosed`; the stamped date is the date checked at click).
- `markInvoiceSeqCancelled` marks the event ANULADA with `fecha_anulacion` and the cancellation XML: rows already ANULADA keep state and date, an empty cancellation XML is filled by a later call, and the closed-quarter check is inside the statement. A failed local write after AEAT accepted is shown in red and the event still shows ANULADA so it cannot be cancelled twice.
- An invoice the real AEAT never held: `cancelLocally` (§Going to production).
- Closing is refused while the request is in flight; `m_pendingCancelId` guards re-entry.

### Rectificar (`RectifyInvoiceDialog`, Facturas → Rectificar factura Verifactu…)
- Operator picks tipo (R1-R5), modo (S / I), date, corrected total or delta. A difference never takes the invoice to 0 or below: original + earlier differences (`rectifiedDifferences`, refused and cancelled ones left out) + the new one must stay above 0, else Anular.
- Refuses a date in a closed quarter or before the original payment (`ticketLastPaymentDate`); the facade also refuses one before production; refuses while a rectificativa or cancellation of the ticket is pending; refuses a rectificativa with the gateway in PRODUCTION.
- Before sending it claims the number with a placeholder `ingresos` row (`nextRectificationNumber`, `R-<n>`) linked by `verifactu_rectifies_n_recibo`; a refused one of the same ticket is reused under its own number. On acceptance of a substitution the original's rows become `RECTIFICADA` (in an open period). A failed local mark after AEAT accepted stays in red.
- Closing refused while in flight; `m_pendingRectifyId` guards re-entry.

### Exportar registros AEAT (`AeatExportDialog`)
Format and scope: requirement 7 of [verifactu-requirements.md](./verifactu-requirements.md). Records come from `sql_lite::aeatExportRecords`; `writeAeatExportXml` (`src/app/aeatexport.cpp`) writes the file.

### Printing (`Imprimir::buildTicket`)
- Rasters the QR at the bottom of the receipt (`TicketRenderer::printableQr`: 30-40 mm, whole dots per module, quiet zone; `EscPosBuilder::rasterImage`, `GS v 0`). A QR `printableQr` cannot read back is printed as received (scaled down only when wider than the paper) with its leyenda.
- `resolveQrCode()`: the in-memory pixmap if present; else the QR drawn locally (`AeatQr`) from the queued direct record or the stored `verifactu_url_qr`, with either connection. Only a gateway reprint of a row with no stored URL calls `generateQRAsync()` and waits up to 5 s (no QR on a timeout, logged).
- The QR covers only the paid rows of one payment event (none for rows of several); a ticket with no printable rows builds nothing.
- A complete invoice (factura completa) needs the recipient's address: without it the event is listed as not printed ("falta la dirección del cliente").

## Environments

| Environment | Validation URL (QR host) |
|-------------|--------------------------|
| TESTING / pre-production | `https://prewww2.aeat.es/wlpl/TIKE-CONT/ValidarQR` |
| PRODUCTION | `https://www2.aeat.es/wlpl/TIKE-CONT/ValidarQR` |

The gateway has one endpoint for both (its ServiceKey decides); endpoints and payloads: [rest_api.md](./rest_api.md).

## Common error scenarios

| Situation | Where | Resolution |
|-----------|-------|------------|
| `INVALID_CONFIG` at save | Paid ticket refused with the reason; unpaid tickets save | Configure NIF / name / ServiceKey |
| `NETWORK_ERROR` at save | Row stays `PENDIENTE` (never `ERROR`) | Start-up recovery; a duplicate answer is reconciled with Consultar en AEAT |
| `8002` CompanyName required (gateway) | `errorDescription` | Set `verifactu.company_name` |
| `8003` InvoiceID duplicate | `errorDescription` | `n_recibo` is unique per issuer; usually an accidental re-submission |
| `3002` on cancel | `CancelInvoiceDialog` | TESTING purges records: submit a fresh invoice first. In production: AEAT holds no invoice with that InvoiceID and date (the dialog already retries an old seq-0 invoice with its reception date); check with Consultar en AEAT |

## Security

- Certificate: read from the Windows personal store by thumbprint (nothing secret stored) or imported in memory from a .pfx whose password is DPAPI-encrypted (`AppSettings::aeatCertificatePassword`). It is only presented in the TLS handshake with AEAT. Tests use throwaway self-signed fixtures and a local fake server, never a real certificate or AEAT.
- The ServiceKey is DPAPI-encrypted in `~/.laideal_settings.json` (`dpapi:v1:`, per Windows user and machine: re-enter it after a reinstall or on another account); legacy plaintext is migrated on load.
- Never mix real data into TESTING. With the gateway the ServiceKey, not `verifactu.environment`, decides where invoices go.
