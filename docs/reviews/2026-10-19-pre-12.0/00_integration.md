# 00 - System integration review, release 12.0

Supervisor: system-integration reviewer. Branch `feature/aeat-direct-investigation` @ 6f9c1dd, 107 commits ahead of `master` (11.2). Read-only: nothing built, no ctest, no shop DB, no AEAT / IreneSolutions contact. Module reports consolidated: **18 of 18** (add_garment, app, appsettings, backup, contabilidad, facturas, imprimir, listado, logging, printing, recog_prendas, reporthtml, sql_lite, tableview, uikit, updater, verifactu-direct, verifactu-gateway). verifactu-direct arrived last and is merged into sections 2, 3 and 6.

Note on the help-desk role: this session had no `ListAgents` tool, and `SendMessage` to a "Review: <module>" name did not resolve. No cross-module message could be delivered, so the cross-module notes are consolidated in this report instead. Several reviewers hit the same limit (verifactu-gateway: "could not reach that reviewer").

---

## 1. Executive summary

- **Overall health: good core, rough edges at the seams.** The Verifactu core (estado mapping, settled-row guards, closed-quarter guards in `sql_lite`, append-only `accounting_closures`, direct-client chain in one transaction, the UI-test/`--fake-aeat` release guard) is careful and well tested (26 CTest suites, a strong e2e bench, UI smoke A-E, the new self-check).
- **The real risks are cross-module.** They sit where a window keeps state across an async AEAT request (Anular / Rectificar / Cobrar), where `ingresos` is written outside `sql_lite` (7 raw statements skip its guards), where nothing stops two app instances on one DB, and where errors turn into "empty" (Contabilidad listings, `nextTicketNumber`, gateway replies). Each can leave the books and AEAT disagreeing.
- **Docs drift is broad.** `architecture.md` does not mention the direct AEAT client at all, the README configuration table has the wrong JSON keys, and the tracker contradicts itself on the production path.
- **Hygiene: shop personal data is in the public repo.** The issuer's NIF appears in the tracker, the investigation doc and a test, and the CSV of an AEAT written request is published.
- **Verdict for 12.0 (shop on AEAT TEST): not ready to tag yet, but close.** About 15 small (S) fixes, the release mechanics (notes, version, declaración date) and one decision (which connection the shop runs on 12.0) stand between it and a safe release. No L-sized work is required before 12.0. Estimate: 2-4 days of S fixes plus tests, then the `/release` gates (UI smoke, M6 pre-production run, compliance auditor on `11.2..HEAD`).

---

## 2. Release blockers / must-do before 12.0

### 2a. Release mechanics (hard gates in CI or `/release`)

1. ⏳ **DEFERRED to `/release`; drafts in the tracker.** **Release notes 12.0** are missing from `releases_notes.txt` and `releases_notes_es.txt`. The top section is 11.2, and `ci.yml:238-247` fails the release without them. 12.0 is large: direct AEAT client, self-check, the menu rework, decimal comma, refused-invoices link. The notes must tell the shop what changes for them on TEST.
2. **Version bump:** `CMakeLists.txt:2` still says `VERSION 11.2`.
3. **Declaración responsable date:** `src/verifactu/producerdeclaration.cpp` (`currentSignature`) still has a pending date. Set it on release day (tracker row 3, release SKILL step 1). Until then Req. 8 is not really "COVERED", although the summary table in `verifactu-requirements.md:131` says it is.
4. **Gates:**
   - `verifactu-compliance-auditor` on `git diff 11.2..HEAD -- src/`
   - `ui-smoke.yml` green
   - smoke part 1, including **M6**: the real pre-production run with the certificate, because 12.0 changes the direct client
   - `ctest` green in CI
5. ✅ **FIXED 19-10-2026: (a), the gateway on TEST in 12.0; the direct client for the later production switch.** **Decide and write down which Verifactu connection the shop runs on 12.0.**
   - The default is still the gateway: `verifactu.connection` is unset, so it means `irenesolutions` (`appsettings.cpp:367`).
   - The direct client in pre-production needs the issuer's certificate on the shop PC, which is not there yet (tracker row 4, item 3).
   - The tracker contradicts itself. `progress_tracker.md:23` says "Next: switch to PRODUCTION once IreneSolutions answers"; `:42` says production goes through the direct client and the IreneSolutions request "becomes moot".
   - Choose: (a) the gateway on TEST in 12.0, with the direct switch later; or (b) the direct client in pre-production from 12.0, which needs the certificate installed and an install-day checklist.
   - With (a), the gateway P0 items below matter more. Align the README, `docs/modules/verifactu/README.md:3/156/191` and the tracker with the choice.

### 2b. Repository / privacy (the repo is public; the tag publishes)

6. ✅ **FIXED 19-10-2026** (HEAD cleaned; history kept, owner's decision). **Remove shop personal data and AEAT access codes from HEAD:**
   - The issuer's NIF: `docs/progress_tracker.md:42`, `docs/modules/verifactu/aeat-direct-investigation.md:151,161`, `tests/test_e2e_app.cpp:1824` (use AEAT's test NIF instead).
   - Her full name: `src/verifactu/aeatcertificate.h:22` (comment).
   - The INFORMA+ request references and the **CSV of a written request** (`progress_tracker.md:39`, `aeat-direct-investigation.md:171-172`). A CSV is a document-verification code at the AEAT sede, so do not publish it.
   - The shop address hard-coded as a default (`appsettings.cpp:103-108`) contradicts the tracker's claim "never in the source" (row 3). Decide and document; appsettings #11.
   - Germán's own NIF in `aeatrecord.cpp:239` is the producer's data, which the record legally carries: acceptable.
   - Rewriting history is the owner's call.

### 2c. Correctness / compliance - books vs AEAT can diverge (all S unless noted)

✅ **FIXED** 7. **Anular / Rectificar keep "loaded ticket" state during the in-flight request** (app A1, A2). A second search marks the WRONG ticket ANULADA / RECTIFICADA. Also: **Rectificar reports success with no local row** when the placeholder INSERT failed (app A3). Locations: `cancelinvoicedialog.cpp:82-90,229-293`, `rectifyinvoicedialog.cpp:169-176,565-567,611-663`.
8. **PayDialog Esc / title-bar X during "Enviando a AEAT..."** drops the payment while AEAT may hold the invoice (recog_prendas #1, `pay_dialog.cpp:287-412`). The same class of problem: Anular / Rectificar closed in flight (app A7).
9. **PayDialog order of operations** (supervisor finding). Cobrar submits to AEAT before the rows are stored as paid; with the direct client the record is chained and queued in `aeat_records` first. `MainWindow::saveTicket` does it the other way round: rows first (PENDIENTE), then submit (`mainwindow.cpp:1130-1137`). A crash or power cut between the two leaves an AEAT record for rows still SIN COBRAR. Locations: `pay_dialog.cpp:261-292` before `:333-358` (`persistPayment`). Fix: mark the rows paid + PENDIENTE before `submitSimplifiedInvoiceAsync`, then patch them on the reply, as the save does. M; owner recog_prendas + auditor.
10. **No single-instance guard** (app A4, `main.cpp:78-96`). Two windows on one DB give duplicate `n_recibo` (MAX+1 twice) and, with the direct client, a forked hash chain. Fix: a `QLockFile` next to the DB, never taken by `--self-check`.
11. **Silent empty Contabilidad can lock a quarter** (sql_lite F1 = contabilidad High). The listings return empty on a query error (e.g. SQLITE_BUSY during the backup `VACUUM INTO`), the report shows zeros, and "Bloquear" closes the quarter. Locations: `sql_lite.cpp:1945-2104`, `contabilidad.cpp:232-291`. M.
12. **`nextTicketNumber` returns 1 when the read fails** (sql_lite F10, `sql_lite.cpp:1271,416-443`). With the gateway (`highestAtAeat` 0) this gives a duplicate number (Req. 2).
13. **Gateway client** (if the shop stays on the gateway in 12.0):
    - no transfer timeout on async calls (`verifactumanager.cpp:322-328`); the docs claim 10 s;
    - `ResultCode 0` without a CSV becomes ENVIADA with no CSV (`verifacturesponse.cpp:110-157`);
    - an HTML / non-JSON body becomes a definitive ERROR (`:116-124`).
14. **Imprimir reprint from Recogida with no row clicked** prints one factura that merges every payment event, with a QR whose amount includes unpaid rows: the QR of an invoice AEAT never received (imprimir F2, `imprimir.cpp:119-139,225-231`; same as printing F18). Also F1: a blank recibo for "650 " or a fully voided ticket.
15. **The direct client's NumeroInstalacion and chain marker are not persisted** until some unrelated `save()` (appsettings #1, `appsettings.cpp:378-389`). A restart can change NumeroInstalacion in the records' SistemaInformatico. Must-fix if the shop runs direct; P1 otherwise. Pending verifactu-direct.
16. **Chain gate only at UI level** (app A5, A6). Anular and the startup "Reintentar" skip `numberingBlockedReason()`, so a cancellation can chain onto a stale previous record before the first sync. Better: enforce it in `AeatDirectBackend` itself.
17. **Direct client: lost cancellation reply.** A resent `RegistroAnulacion` that AEAT answers as a duplicate in state "Anulada" is treated as a failure for every record kind (verifactu-direct High, `aeatdirectbackend.cpp:704-709`). The invoice stays not ANULADA locally (still counted as income) and Anular loops. The fake server answers `Correcta`, so no test sees it. P0 if the shop runs direct, else P1. Confirm AEAT's real answer in the M6 pre-production run (cut the network after the send).
18. **Direct client: a first sync with an empty `SistemaInformatico`** (AEAT omitted the block) silently skips our own records and would start a new chain with `PrimerRegistro` (verifactu-direct Medium, `aeatdirectbackend.cpp:271`). S: count it as unusable so the sync fails.

### 2d. Strongly recommended before 12.0 (cheap, user-visible)

- **Enter on a focused secondary button / Cerrar presses the PRIMARY button** (uikit Medium, `uikit.cpp:64-90,137-142`), which can be Cobrar / Reenviar / Anular. One `click()` on the focused button fixes it.
- **Backup:**
  - two backups in the same second delete the good one;
  - a partial `laideal_*.db` after a crash counts as a valid survivor and is picked by the self-check;
  - manual and auto backups run concurrently on the same connection names;
  - AppSettings is mutated off the GUI thread.
  (backup High x4, `backup_manager.cpp:84-167`; all S.)
- **Pre-migration snapshot** (supervisor). `migrateDatabase` runs in the MainWindow constructor (`mainwindow.cpp:431`) and the day's backup only 3 s later on a worker (`:122-143`). After an update, the first start migrates the shop DB before any 12.0 backup exists, and `migrateDatabase` is `void` (app A14). Take a `VACUUM INTO` snapshot before migrating when the version changed (store `app.last_run_version`), or at least when `needsBackup()`. S-M.
- **Packaging guard** (supervisor + updater #30). The `windeployqt` assertion (`ci.yml:195-210`) misses `sqldrivers\qsqlite.dll` (no DB without it) and the TLS backend `tls\qschannelbackend.dll` (the updater and the gateway fail silently over HTTPS). `release.ps1` has no assertion at all.
- **PII in the log** sent to support: client names and phones (logging Medium, `add_garment.cpp:322`, `sql_lite.cpp:589-619,1447`).
- **Production-switch safety in Configuración** (appsettings #2, #3):
  - leaving the direct connection keeps `aeat_direct_production=true`, so coming back skips the confirmation;
  - the gateway's PRODUCCION box has no confirmation (and it only changes a cosmetic URL: gateway Medium).

---

## 3. Cross-module & architecture findings

### 3.1 Module boundaries: CMake link graph vs includes vs docs

| # | Finding | Where | Proposal |
|---|---------|-------|----------|
| X1 | The lowest layer depends on Verifactu. `sql_lite` and `add_garment` include `../verifactu/verifactutypes.h` by relative path without linking `verifactu`. The dependency is invisible to CMake and inverts the layering (`sql_lite` is meant to be the base). | `src/sql_lite/sql_lite.cpp:3`, `src/add_garment/add_garment.cpp:4` | Move `VerifactuEstado` + helpers to a header-only INTERFACE target (e.g. `src/verifactu/types/` or into `sql_lite`'s `ingresos_schema.h` family) linked explicitly by both. |
| X2 | Direct-backend internals leak past the facade. `PayDialog`, `CancelInvoiceDialog`, `RectifyInvoiceDialog`, `MainWindow` and `main.cpp` include `aeatdirectbackend.h`, `aeatstore.h`, `aeatresponse.h`, `aeatcertificate.h` and `aeatselftest.h`, and call `directBackend()`. The docs present `VerifactuIntegration` as the only entry point. | `pay_dialog.cpp:2`; `cancelinvoicedialog.cpp:2-3`; `rectifyinvoicedialog.cpp:2,23-24`; `mainwindow.cpp:28-30,571-585` | Add facade methods (`holdsPendingRecord`, `storedOutcome`, `qrBeforeReply` already exists) and keep `aeat*` headers private to the `verifactu` target (PRIVATE include dir). This also gives A5/A6 a single place for the chain gate. |
| X3 | `sql_lite` reads the `aeat_records` table (owned by `AeatStore`) with literal column names, and failures are ignored. Two divergent test copies of that table exist. | `sql_lite.cpp:1213-1214,1892-1909`; `test_sql_lite.cpp:607,1425` (sql_lite note) | `AeatStore` exposes `pendingCount(env)` and `usedSeqs(ticket, env)`; `sql_lite` stops touching the table. |
| X4 | The income Verifactu base is computed by the expense dialog's static `Facturas::taxBaseFromGross`, and inline in 2 other callers. The amounts reach the gateway unrounded. | `mainwindow.cpp:885-887,964`; `pay_dialog.cpp:263`; `recog_prendas.cpp:1054` | One `splitGross(gross, rate)` in a neutral money helper (`sql_lite` money functions or `verifactu`), rounding the base to cents and setting tax = gross - base. Used by the facade (gateway Medium), Facturas, Contabilidad and the renderer. Lock it with printing T9. |
| X5 | Undeclared or odd link edges. `appsettings` links `printing` (Status API diagnostics); the docs say printing is "used only by imprimir" (`architecture.md:110`). `verifactu` is `add_library(verifactu ...)` with no `STATIC` and the plain `target_link_libraries` signature (everything PUBLIC). `imprimir` links `verifactu` PUBLIC. | `src/appsettings/CMakeLists.txt:13`; `src/verifactu/CMakeLists.txt:56-66` | Add `STATIC` and the keyword signature; document the appsettings->printing edge. |
| X6 | `ingresos` has many writers. Seven raw `UPDATE/INSERT ingresos` statements sit outside `sql_lite`. `rectifyinvoicedialog.cpp:611` lacks the settled-row guard (a late duplicate rejection can turn ENVIADA into ERROR); `pay_dialog.cpp:345` has no closed-quarter predicate; `:577` bypasses `moneyText`. | sql_lite F5; `rectifyinvoicedialog.cpp:447,524,577,611`, `pay_dialog.cpp:345,393`, `pendingsubmitsdialog.cpp:177` | Move each into a named `sql_lite` function with the standard guards (one writer per file, Sonnet). Then the DB triggers of sql_lite F4 as defence in depth (auditor first). |
| X7 | Shared default connection, open/close pattern. About 40 `sql_lite` functions plus about 25 module call sites `db.open()` / `db.close()` the same default connection, closing it under other windows' models. Combined with lazy `QSqlQueryModel` fetching, this causes the 256-row cut in Recogida phone/date/ticket searches (recog #2) and in the Listado ingresos filter/sort (listado, tableview). | sql_lite F13; recog_prendas #2; listado Medium; facturas Medium; imprimir F11; app A22 | A `ConnectionScope` RAII (closes only what it opened) plus a "load all rows then close" helper for query models. Mechanical (Haiku/Sonnet) once designed. |
| X8 | Modal boxes from the data layer. `sql_lite` shows 7 or more `QMessageBox`es (`readMaxValue...`, `readGarmentPrice`, ...). They pop up in dialogs that promise "no pop-ups" and in the self-check, and they block failure-path tests. | sql_lite F18; facturas; add_garment High; contabilidad | Return a status and let the caller show it in its ResultPanel. Removes the Widgets dependency from `sql_lite`. |

### 3.2 Start-up and data flow (`main.cpp` -> MainWindow -> `migrateDatabase` -> Verifactu init -> timers)

Actual order:
1. `AppLogger::install()`
2. `settings->load()`. This can write: re-encryption and first-run defaults (appsettings #16, app A18).
3. `--self-check` branches off here.
4. DB path prompt.
5. `MainWindow`: `buildUi` -> `migrateDatabase` (`void`, result ignored) -> `initializeVerifactu` (also `settlePendingRectifications` and `continueDirectChain`) -> refused label + 30 s timer -> updater at 1.5 s -> backup worker at 3 s -> PendingSubmitsDialog at 4 s.

Issues:
- No pre-migration backup (see 2d) and no visible migration failure (A14).
- The refused label is created after `initializeVerifactu()`, but `onRecordSettled` dereferences it (app A23, latent).
- The backup worker captures `this` with no `wait()` in `~MainWindow` (backup Medium, app A24). An in-app update quits the app (`qApp->quit()` in `updaterdialog.cpp:113-120`) and can therefore cut a running `VACUUM INTO`, leaving the partial file of backup High #2.
- PendingSubmits "Reintentar" fires at 4 s, before the first chain sync can finish, and ignores the empty reqId (A6).
- Nested event loops (save 3 s `mainwindow.cpp:1146-1162`, imprimir 5 s `imprimir.cpp:244-273`) leave the UI re-entrant (A12: Limpiar during the wait prints the next number; imprimir F6).

### 3.3 Cross-module invariants

| Invariant | Status | Gaps (source) |
|-----------|--------|---------------|
| Amounts stored with a dot, shown with a comma | Display side done in 6fe962b (`displayMoney` / `displayDecimal`, delegates, `AmountSpinBox`). | **Three different policies for a stored comma amount:**<ul><li>`moneyText` normalises it (`sql_lite.cpp:1481`);</li><li>Contabilidad flags it as invalid (`:1977`);</li><li>Imprimir (`imprimir.cpp:227,322` `QVariant::toDouble` gives 0) and the SQL `SUM(importe)` feeds to AEAT (sql_lite F21, numeric prefix) take it silently.</li></ul>`moneyText("1.234,50")` becomes 0.00 silently (F6). Float sums: `recog_prendas.cpp:671` (#10), genlistado (listado Medium). Rounding: Contabilidad IVA is unrounded while AEAT records are rounded per invoice (contabilidad Medium). The base is sent unrounded to the gateway (X4). **Proposal:** one strict `parseStoredAmount(text, &ok)` in `sql_lite`, used by every reader (with the comma flagged everywhere), plus X4 for the split; the self-check counts comma rows on real data. |
| Quarter lock (`accounting_closures`) | Strong in the `sql_lite` writers and the Listado gastos lock. | Missing or weak in: `pay_dialog.cpp:345` raw UPDATE (X6); `applySettledVerifactuCancellation` dates "today" with no open-period check (sql_lite F3); AddGarment checks only the payment date (add_garment Medium); Contabilidad lets the current, unfinished quarter be closed, which blocks every Cobrar today (contabilidad Medium); regenerating a closed quarter overwrites the filed PDF (contabilidad Medium). |
| Verifactu estado lifecycle | Mapping centralised (`verifactutypes.h`, `verifactuEstadoForResult`); settled-row guard in `updateTicketVerifactuFields`. | Unknown or new strings fall back to PENDIENTE (`verifactutypes.h:42-46`: a resend candidate). Gateway SUCCESS without CSV becomes ENVIADA; HTML becomes ERROR (2c-13). Reply-time `INVALID_CONFIG` leaves the estado empty in PayDialog (recog #4). `reconcileVerifactuFromAeat` lacks a `COALESCE` for NULL estado (sql_lite F11). The rectificativa reply has no guard (X6). The docs say the enum lives in `verifactumanager.h` (wrong in 5 docs). |
| `n_recibo` numbering | `MAX(CAST AS INTEGER)` since 11.2; the direct client keeps the number above AEAT's. | No single-instance lock (A4). A read failure gives 1 (F10). Proxies sort `n_recibo` as text, so "newest first" breaks at the 99999 -> 100000 rollover (tableview High, listado Medium; shop numbers are about 31k today, so P2). AddGarment assumes seq 0 (add_garment Medium). |
| No shop data in the repo | `.gitignore` excludes `*.db`, `*.log`, `.venv`, `build*`; the test certificates are self-signed and documented. | Personal data in HEAD (2b). `.gitignore` lacks `*.sqlite`, `*.pfx` / `*.p12` (with a `!tests/fixtures/aeat-cert/*.p12` exception) and `*settings*.json` copies, so a real certificate or settings copy dropped into the tree is committable. |
| Nothing reaches the real AEAT in tests | All network suites use `setEndpointOverride` / fake servers (`tests/support/e2efixture.h`, fakeaeat / fakeverifactu); the UI build accepts only `127.0.0.1` (`main.cpp:47-50`); the release refuses a `--fake-aeat` exe (`ci.yml:179-188`, `release.ps1:93-98`), and ui-smoke checks the marker. | It rests on each fixture remembering the override; there is no global tripwire. Proposal P3: in test builds, `AeatTransport` / `VerifactuManager` refuse any host other than `127.0.0.1` when an env var such as `LAIDEAL_TEST=1` is set by CTest. |

### 3.4 CI / release pipeline / installer

| # | Finding | Where | Proposal |
|---|---------|-------|----------|
| C1 | The release does not require the UI smoke. It is a manual gate in `/release`. `ui-smoke.yml` runs only on pushes touching `tests/ui/**` / `tests/tools/**`, so a `src/` change never triggers it. | `.github/workflows/ui-smoke.yml:13-20` | Add `pull_request: branches: [master]` (it is the release PR) and optionally a nightly; or have the `release` job check that the last ui-smoke run on the tagged SHA succeeded. |
| C2 | The deploy assertion misses `sqldrivers\qsqlite.dll` and `tls\qschannelbackend.dll`. | `ci.yml:195-210` | Add both. |
| C3 | `release.ps1` is not "the same pipeline": no `ctest`, no zip, no deploy assertion. Its header still says "MinGW Makefiles". CONTRIBUTING says it produces a zip. | `releases/release.ps1:4-12,113`; `CONTRIBUTING.md` "Local fallback" | Add ctest and the DLL check, or reword the docs. |
| C4 | Typo `^r?` (meant `\r?`) in the notes regex, copied three times. Harmless today. | `ci.yml:238`, `CONTRIBUTING.md` local fallback, `release/SKILL.md:176` | Fix in all three. |
| C5 | Installer: dead `.myp` file association (the app ignores `%1`); no `[InstallDelete]` for stale DLLs or plugins of earlier versions (`ignoreversion recursesubdirs` only adds); nothing prompts the shop to run "Comprobar tras actualizar" after an in-app update (updater #29). | `releases/laideal.iss:16-18,63-69,73` | Drop the association; `[InstallDelete] Type: filesandordirs; Name: "{app}\plugins"` etc.; an optional unchecked `[Run]` entry for `--self-check`. |
| C6 | Updater trust: the downloaded installer is run with no hash or signature check (updater High). The release job could publish `.sha256` or rely on the asset `digest`. | `updater.cpp:174-189`; `ci.yml` release | P1 (M): hash; signing later (L). |
| C7 | `windows-latest`, chocolatey MinGW and `jurplel` are not pinned to a runner image; already mitigated with retries and the runtime check. | `ci.yml` | Accept; consider `windows-2022` pinning. |

---

## 4. Global documentation & repo findings

| # | Doc | Drift / problem | Fix |
|---|-----|-----------------|-----|
| G1 | `docs/architecture.md` | **No direct AEAT client anywhere.** The module tree (8-24), the "Verifactu Integration" section (288-311: "VerifactuConfig QSettings INI", one REST endpoint, `www2.aeat.es`) and the schema have no `aeat_chain` / `aeat_records` / `aeat_chain_sync` tables (`aeatstore.cpp:65-77`). Also stale: "9 verifactu columns" (186; it is 13); `VerifactuEstado` in `verifactumanager.h` (216,311; it is in `verifactutypes.h`); backup directory, file name and QMessageBox (149-158); updater `%TEMP%` (135); the data-flow diagram (256-284) lacks `qrBeforeReply`; the app dialogs (Refused, AeatExport, SelfCheck) are missing from the tree; `printing` "used only by imprimir"; `tableview` "all columns"; the `facturas` / `empresas` tables (252) do not exist; Known Issues lists only three fixed items. | Rewrite the Verifactu section as "two backends behind `VerifactuIntegration`" with a link to `modules/verifactu/README.md` §Direct; add the three `aeat_*` tables; replace duplicated paragraphs (Contabilidad 84-87, backup, updater) with links to the module docs (Obligation 3: no duplication). |
| G2 | `docs/ai_agent_instructions.md` | Line 9: "Qt 5.15+", "DB at a hardcoded path" (stale since AppSettings). Line 17-19: CMake 3.5 / "MinGW Makefiles" (the README insists on Ninja). The file map (22-48) lacks the direct client (`aeat*`), selfcheck, RefusedInvoicesDialog, AeatExport, `verifactutypes.h`. Line 110: "Verifactu call in `on_pb_save_clicked()` → `verifactuSubmitInvoice()`" (also PayDialog, AddGarment, Rectify, Cancel). Line 112: factura "stored in `facturas` table" (it is `gastos`). | Update; keep it the 1-page briefing. |
| G3 | `README.md` | Configuration table (129-136): wrong keys `db.path`, `app.ivaRate` (not a setting), `report.*`, `verifactu.name`, `verifactu.serviceKey`, `verifactu.production`; misses about 15 real keys incl. `verifactu.connection` and `aeat_*` (appsettings drift table). Features: "HTML reports" (they are PDF); the Verifactu bullet does not mention the direct client or the self-check. Requirements say "Qt 5.15+ / MSVC or MinGW" while CONTRIBUTING pins Qt 6.4.3 + MinGW 11.2 (`winhttp` / `ncrypt` / DPAPI make it Windows-only; Qt 5 is untested). Tests: "four kinds" with five rows. | Fix the table from `appsettings.cpp`; state the supported toolchain once. |
| G4 | `docs/INDEX.md` | "Root CMake — project 8.0" (11.2); glossary `VerifactuEstado` lists 4 of 6 states and the wrong header; "Factura: stored in `gastos` via Facturas form" conflates the supplier form with the sales invoice; `backup.md`, `uikit.md` missing from the Documentation Files table; "QR via REST `/GetQrCode`" (the direct client draws it locally); `VerifactuManager::processResponse()` (now `parseVerifactuResponse`); "All configurable settings (paths, IVA, ...)". `test_versioncompare` is correct (the CTest exe name of `test_updater.cpp`); the uikit reviewer's doubt is unfounded, but say "exe of test_updater.cpp". | Update. |
| G5 | `docs/README.md` | The structure tree lacks `modules/backup.md`. There are no module docs for appsettings, updater, logging, tableview, reporthtml or the app dialogs / self-check; `mainwindow.md` is stale for Anular / Rectificar (app D2-D5). | Add `docs/modules/app.md` (app reviewer) and short pages or sections for the others, or say explicitly that `architecture.md` is their reference. |
| G6 | `docs/progress_tracker.md` | "Active branch: develop" (the work is on `feature/aeat-direct-investigation`). The "Next" line contradicts the production row (2a-5). No "12.0 release prep" item. The findings of this review must be filed there (memory rule: fix or file right away). | Update Current Status; add a Blocking section for 12.0 with the P0 items of section 6. |
| G7 | `docs/modules/verifactu/verifactu-requirements.md` | Req. 1: the "per-row `verifactu_hash` chain gives a tamper-detection signal on read", but no code recomputes or verifies the chain (grep finds no verifier; verifactu-direct Medium). Req. 4: "QMessageBox warning on failure" (it is the result panel). Req. 5: "hardcoded DB path". Req. 3 / 7: `processResponse()`. Req. 8 summary COVERED vs the pending date. Req. 9: leyenda "guarded by ENVIADA" (also direct PENDIENTE). | Correct; for Req. 1, either implement a chain verifier (self-check M7, app reviewer) or drop the claim. |
| G8 | `CONTRIBUTING.md` / `.claude/skills/release/SKILL.md` | The release skill says "The root README.md is the source of truth" and cites "README step 3/4/5" (the workflow lives in CONTRIBUTING). The user's auto-memory says "master only fast-forwards at release time", while CONTRIBUTING and the skill require a merge commit (`--no-ff`). | Point the skill at CONTRIBUTING; the owner should reconcile the memory note (outside the repo). |
| G9 | `docs/testing/README.md` | The e2e row says "against a fake Verifactu server" (there is also the fake AEAT). The layers table omits the self-check as a fifth layer, although the root README lists it. | Align with the root README. |
| G10 | `docs/modules/verifactu/rest_api.md`, `docs/modules/printer/04_*.md`, `05_*.md` | Heavily stale. `rest_api.md` describes non-existent APIs and even advises logging the ServiceKey (gateway Low, `:584-588`). Printer 04/05 are Excel-era. | Delete the debugging advice now; mark 04/05 as "historical" or archive them; trim `rest_api.md` to the fields actually sent. |
| G11 | Repo hygiene | Unused `icon/lavadora_fancy.ico`, `lavadora_nobg.ico`, `lavadora.png`, `change_icon.ps1`; stale remote branch `origin/feature/recog-prendas-edit-qty-service-price`; `CMakeLists.txt.user`, `laideal_docs.egg-info/`, `ui-smoke-out/` correctly ignored; `.gitignore` gaps (3.3). `completed_milestones.md` at 611 lines: check the archive rule of `/update-docs`. | Clean up in a docs/hygiene commit. |

---

## 5. Test strategy across layers: gaps and where each belongs

| Layer | Purpose | Strong today | Main gaps (where they belong) |
|-------|---------|--------------|-------------------------------|
| **Unit** (`tests/test_*.cpp`, pure helpers, throwaway DB) | Logic, `sql_lite` writers / readers, parsers, renderer bytes | `sql_lite` (about 100 slots), AEAT hash / record / response / store, `escpos`, Contabilidad figures | No suite at all for: AppLogger, AddGarment, Imprimir (engine), Updater (beyond `compareVersions`), FilterWidget / TableView, `backupsToPrune` boundaries and `performBackup`. Failure paths: DB errors in the listings (F1), `nextTicketNumber` failure, `moneyText` garbage, `migrateDatabase` twice-run idempotence over a full fixture, settings load / save / corrupt / migration, NumeroInstalacion persistence, CP858 table vs an independent reference (printing T1: today's test is circular), printed tax split == AEAT record math (T9), `n_recibo` numeric sort. **Blocker for many of these:** `QMessageBox` inside `sql_lite` (X8) and missing seams (Updater `parseRelease`, printer transport hook, log path, backup `now`). |
| **E2E** (`test_e2e_*`, real objects + fake gateway / fake AEAT) | Flows across windows and the network | Save, Cobrar, timeout adoption, cancel / rectify (S mode), direct chain gate, refused list, export | **State races:** search during in-flight Anular / Rectificar (A1, A2), Esc in PayDialog (recog #1), Limpiar during the 3 s wait (A12), failed placeholder INSERT (A3). Rectify by differences, R1-R4 through the gateway, Anular with a live rectificativa (A9). Gateway: dropped / stalled / HTML / success-without-CSV replies (the fake server already has `drop`, unused), re-init while in flight. Recogida >256 rows and search modes. Imprimir multi-event reprint from Recogida (F2), cancelled-invoice reprint (F3). Backup manual + auto. Contabilidad Mensual / Anual, no-movements, regenerate-closed. |
| **UI smoke** (installed UI-test build, pywinauto, CI) | Real widgets, real keyboard, installer, captured printer queue | Blocks A-E, the pre-production dry run | Real cancel + export on the installed exe (only the local script does it); Enter / Tab-to-Cerrar keyboard rules; Configuración round trip by JSON key names and production confirmations; second instance refused (A4); printer offline / unknown queue (printing F5); 58 mm; a 300-garment seed; `--self-check` from the installed exe with a seeded backup (exit code, A18). **CI trigger gap (C1).** |
| **Self-check** (`--self-check`, shop PC, copy of the newest backup) | The shop's real schema / data / printer after an update | M1 migrations, M2 newest ticket, M2b closed quarter, M3 print + QR, M5 installer | Offline direct-client checks: chain integrity (`AeatChainCheck`), outbox age, certificate expiry and NIF, marker vs DB head (verifactu-direct). Run `migrateDatabase` **twice** and fail on any repeated repair line (sql_lite F8e). Settings health: DPAPI decrypts, certificate resolves, `database.path`, floor date (appsettings M0). Backup age > 3 days and backup-name filter shared with BackupManager. Comma / float / iva-rate counts in `ingresos` / `gastos` (where the money invariants break on real data). Chain verifier over `aeat_records` (M7, pending verifactu-direct). Export round trip (M6). Use the real Recogida search instead of re-implementing `LIKE` (recog). Recompute every closed quarter and compare with figures stored at close (contabilidad, needs columns: M). TLS / updater reachability (report only). **Rule:** the self-check stays read-only; `main.cpp` must not let `load()` write before it (A18 / appsettings #16). |
| **Manual** (smoke part 1) | Looks, real AEAT pre-production | M4, M6 | M6 is mandatory for 12.0. Add one look at an "I grave" / accent line until printing T1 exists. |

Rule of thumb for placing a new test: pure logic goes to unit. Anything with a timer, reply or second window goes to e2e with the fake servers. Anything needing the native keyboard, installer or spooler goes to UI smoke. Anything that depends on the shop's real data or printer goes to the self-check, read-only.

---

## 6. Consolidated prioritized backlog

Priority: **P0** blocker for 12.0 / **P1** before the 12.0 release (strongly advised) / **P2** soon (12.1) / **P3** later. Effort S / M / L. Source: the module report file name, or `00` for the supervisor.

| Pri | Module | Finding | Proposal | Eff | Source |
|-----|--------|---------|----------|-----|--------|
| P0 | release | No 12.0 section in `releases_notes*.txt`; CMake 11.2; declaración date pending | Write ES + EN notes; bump; set the date at `/release` | S | 00 |
| P0 ✅ FIXED | release | Shop connection for 12.0 undecided; the tracker contradicts itself (`:23` vs `:42`) | Decide gateway-TEST vs direct pre-production; align tracker / README / verifactu README; install-day checklist | S | 00, verifactu-gateway |
| P0 | release | Gates: compliance auditor on `11.2..HEAD`, ui-smoke green, M6 pre-production run, CI green | Run them | M | 00 |
| P0 ✅ FIXED | repo | The issuer's NIF / name and the AEAT request CSV in the public repo | Remove from HEAD (tracker:39,42; investigation:151,161,171-172; `test_e2e_app.cpp:1824`; `aeatcertificate.h:22`); owner decides on history | S | 00 |
| P0 ✅ FIXED | app | Anular / Rectificar act on the re-searched ticket when the reply arrives (A1, A2) | Snapshot the submitted ticket / seq / id; lock the search while pending | S | app |
| P0 ✅ FIXED | app | Rectificar "success" without a local row (A3) | Abort on a failed placeholder INSERT; check `numRowsAffected` | S | app |
| P0 | recog_prendas | PayDialog Esc / X during the AEAT wait drops the payment (#1); same for Anular / Rectificar close in flight (A7) | Override `reject()` / `closeEvent` while pending | S | recog_prendas, app |
| P0 | app | No single-instance guard: duplicate `n_recibo`, forked chain (A4) | `QLockFile` in `main()` (not for `--self-check`) | S | app |
| P0 | sql_lite / contabilidad | Listings return empty on SQL error, so a zero report can lock a quarter (F1) | `ok` channel; refuse to write / close | M | sql_lite, contabilidad |
| P0 | sql_lite | `nextTicketNumber` returns 1 on a read failure (F10) | Return an error; refuse to save | S | sql_lite |
| P0 | imprimir | Recogida reprint with no row: merged events, QR amount over unpaid rows (F2); blank recibo (F1) | Paid-only sums, refuse multi-seq without a seq, trim, empty-model guard | S-M | imprimir, printing F18 |
| P0* | verifactu-gateway | No transfer timeout; SUCCESS without CSV becomes ENVIADA; HTML becomes ERROR (*P0 if the shop stays on the gateway in 12.0, else P1) | `setTransferTimeout`; submit-without-CSV becomes PENDING; non-JSON becomes NETWORK_ERROR | S | verifactu-gateway |
| P0* | appsettings | NumeroInstalacion / chain marker not saved (#1) (*P0 if direct at the shop) | `save()` when created / set | S | appsettings |
| P0* | verifactu-direct | A lost cancellation reply, resent and answered "duplicate / Anulada", becomes ERROR: books keep the income and Anular loops (*P0 if direct, else P1) | Cancellation duplicate = accepted; fake answers like AEAT; confirm in M6 | S | verifactu-direct |
| P1 | verifactu-direct | A first sync skips records without `SistemaInformatico`, so it may restart the chain with `PrimerRegistro` | Treat as unusable, so the sync fails | S | verifactu-direct |
| P1 | verifactu-direct / imprimir | Reprint QR rebuilt from the current environment: after the production switch, old TEST invoices print a production QR | Reprint from the stored `verifactu_url_qr` (same as imprimir F6) | S | verifactu-direct, imprimir |
| P1 | verifactu-direct | No warning before the certificate expires; expiry mid-run gives an endless generic retry | Warn under 30 days; re-check on a TLS failure (before production) | S | verifactu-direct |
| P1 | verifactu-direct / docs | No chain verifier, although Req. 1 claims detection on read (art. 8.1) | `AeatChainCheck::verify` at start + self-check; fix the wording now | M | verifactu-direct, 00 |
| P1 | recog_prendas | Cobrar submits before the rows are stored as paid (crash window) | Store paid + PENDIENTE first, then submit, patch on reply (like `saveTicket`) | M | 00 |
| P1 | app / verifactu | Chain gate only at UI level: Anular and startup Reintentar skip it (A5, A6) | Enforce in the backend / facade; Reintentar honours an empty reqId | S | app (pending verifactu-direct) |
| P1 | uikit | Enter on a focused secondary / Cerrar presses the primary (possible AEAT send) | `click()` the focused button; restrict to `window()==dialog` | S | uikit |
| P1 | backup | Same-second backup deletes the good one; partial files; concurrent runs; AppSettings off-thread | `.part` + rename; never remove what you did not create; running guard; settings write on the GUI thread | S | backup |
| P1 | app / sql_lite | `migrateDatabase` is void and runs before any backup of the new version | Pre-migration snapshot on version change; return a failure count and show it | S-M | 00, app A14 |
| P1 | CI | Deploy assertion misses `qsqlite.dll` and the TLS plugin; `release.ps1` has none | Add to `ci.yml:195-210` and `release.ps1` | S | 00, updater |
| P1 | printing | Factura simplificada prints no fecha de expedición (QR uses `fecha_pago`); "Factura completa" headed SIMPLIFICADA | Print the issue date; heading per type; auditor decision | M | printing |
| P1 | imprimir | Cancelled / rectified invoice reprints as a normal factura (F3) | Refuse or mark ANULADA; legal decision | M | imprimir |
| P1 | appsettings | Leaving direct keeps production=true (#2); gateway PRODUCCION without confirmation (#3) | Force off on leave; confirm both | S | appsettings |
| P1 | sql_lite | Settled cancellation dated today even in a closed quarter (F3); connection left open (F2); void-date step not idempotent (F7) | `openPeriodDate`; close on all paths; skip dateless rows | S | sql_lite |
| P1 | sql_lite (X6) | Seven `ingresos` writers outside `sql_lite`; the rectificativa reply lacks the settled guard | Move into guarded `sql_lite` functions; tests | M-L | sql_lite, 00 |
| P1 | logging | Client names / phones in `~/.laideal.log` | Drop the PII from the messages | S | logging, sql_lite F22 |
| P1 | recog_prendas | Phone / date / ticket search cut at 256 rows; wrong total (#2) | Drain helper (X7) | S | recog_prendas |
| P1 | recog_prendas / add_garment / app | Payment / reception / rectification dates unbounded (future, before reception) | Max today, min reception; refuse in validation | S | recog_prendas #5, add_garment, app A11, A17 |
| P1 | add_garment | Free-typed or empty garment gives pop-ups and 0 EUR paid invoices; negative size | Exact-name check; refuse 0 / negative; clamp | S | add_garment, sql_lite F14 |
| P1 | recog_prendas | Zero-total Cobrar sent to AEAT; reply-time `INVALID_CONFIG` leaves the estado empty; null integration books without a record | Refuse; treat as PENDIENTE; refuse when null | S | recog_prendas #4, #6, #7 |
| P1 | updater | Installer run without integrity check | Publish `.sha256` / use the asset digest; verify size + hash | M | updater |
| P1 | contabilidad | The current (unfinished) quarter can be closed, blocking today's Cobrar; gastos IVA text parsed to 0 silently | Warn / refuse; strict parse to -1 (flag) | S | contabilidad |
| P1 | docs | `architecture.md` lacks the direct client and `aeat_*` tables; README config keys wrong; tracker status | G1, G3, G6 | M | 00, appsettings, gateway |
| P1 | tracker | This review's findings not filed | File P1-P3 in `progress_tracker.md` (memory rule) | S | 00 |
| P2 | sql_lite / all | Three policies for comma amounts; `moneyText` garbage gives 0; SQL `SUM` prefix; float sums | One strict stored-amount parser; double / cents everywhere; flag commas | M | 00, sql_lite F6, F21, recog #10, listado |
| P2 | verifactu / facturas | Gross-to-base split duplicated, unrounded to the gateway; Contabilidad IVA vs per-record rounding | `splitGross` helper (X4); accountant decision on modelo 303 | M | verifactu-gateway, contabilidad, facturas |
| P2 | architecture | `sql_lite` / `add_garment` include a verifactu header by relative path; facade leaks (X1, X2, X3, X5) | INTERFACE types target; facade methods; `AeatStore` reads | M | 00 |
| P2 | sql_lite | Shared-connection open / close (X7), `QMessageBox` in the data layer (X8) | `ConnectionScope` RAII; return a status | M | sql_lite, facturas, listado, imprimir |
| P2 | sql_lite | `migrateDatabase` hardening (user_version, per-repair transaction, log once) + base schema / unique index / busy timeout | Tracker plan + F8 / F12 | M | sql_lite, tracker backlog |
| P2 | sql_lite | DB triggers for ENVIADA row inalterability (Req. 1 defence in depth) | After X6; auditor on the column list | M | sql_lite F4 |
| P2 | tableview / listado | `n_recibo` text sort (rollover at 100000); `fecha_anulacion` sort; filter scans 10 columns; double filter per keystroke; lazy-load filter | Numeric / date branches; one invalidate; drain | S-M | tableview, listado |
| P2 | listado | Ingresos read-only only through the view flag | Model / proxy level refusal + tests (Req. 1) | S | listado |
| P2 | app | Anular with a live I-mode rectificativa (A9); Rectificar lookup picks an arbitrary group (A10); client autofill copies another client's data (A13 = sql_lite F16) | Refuse / warn; event-based load; exact-match autofill | M | app, sql_lite |
| P2 | app | Anular has no confirmation (A8); Cancel / Rectify not UiKit, unescaped AEAT text (A21) | Confirm; port to UiKit | M | app, uikit |
| P2 | printing | CP858 Ì maps to 0xDD (should be 0xDE) and the test is circular; control characters injected; RAW path says "impreso" when offline | Fix the table + an independent reference test; map C0 to space; `GetPrinterW` status | S-M | printing |
| P2 | backup | Clock-jump pruning, earliest-of-month survivor, same-disk storage, no warning when disabled | Keep the newest N; last-of-month; `last_success` warning; second folder (product) | M | backup |
| P2 | updater | No download timeout / cancel; shared silent flag; process-wide TEMP; URL host not checked | Per the updater report items 2-5 | S-M | updater |
| P2 | appsettings | Corrupt settings overwritten by defaults; non-atomic save; legacy plaintext key file left | `.corrupt` copy; `QSaveFile`; delete legacy | S-M | appsettings |
| P2 | facturas | inf / nan / negative / zero amounts; duplicate invoice; untrimmed fields; free-text servicio | Validation + duplicate check | S | facturas |
| P2 | contabilidad | Regenerating a closed quarter replaces the filed PDF | Temp + rename / new name; keep the original | M | contabilidad, reporthtml |
| P2 | reporthtml / listado | Unescaped business / supplier text in PDFs; prendas PDF result ignored | `toHtmlEscaped`; propagate `writePdf` | S | reporthtml, listado |
| P2 | logging | Rotation only at start; `.old` lost on a rename failure; silent when the file is not openable | In-session rotation; checks; fallback | S-M | logging |
| P2 | CI | UI smoke not triggered by `src/` changes nor required for release (C1); `release.ps1` lacks ctest / zip (C3) | PR-to-master trigger; align `release.ps1` | S | 00 |
| P2 | installer | Dead `.myp` association; stale DLLs never removed; no prompt for the self-check after an update | C5 | S | 00, updater |
| P2 | tests | Self-check additions: twice-run migration, settings health, backup age, money / iva counts, chain verifier, export round trip | Section 5 | M | sql_lite, appsettings, backup, app, listado |
| P2 | tests | E2E state races + gateway failure modes + Recogida search modes | Section 5 | M | app, recog_prendas, verifactu-gateway |
| P2 | docs | ai_agent_instructions, INDEX, docs/README, requirements, release skill drift (G2, G4, G5, G7, G8, G9); rest_api ServiceKey logging advice (G10) | Update; delete the advice | M | 00, all |
| P2 | repo | `.gitignore` lacks `*.sqlite`, `*.pfx` / `*.p12` (fixture exception), settings copies | Add | S | 00 |
| P2 | verifactu-direct | A client SOAP fault refuses a whole batch (up to 1000 records); a duplicate is adopted without checking amount and date; the restore head may be a cancellation (chain fork; GZ44 item 3) | Resend singly after a batch fault; query and compare before adopting; extend the settings marker to the full head | M | verifactu-direct, tracker:39 |
| P2 | verifactu-direct | `NombreRazonEmisor` not truncated to 120 (schema fault for every record); qrcodegen exception uncaught; waiters not answered in two edge cases; WinHTTP up to about 120 s per request, which can hang the exit | `left(120)`; catch; answer from the store; shorter connect timeout / private pool | S | verifactu-direct |
| P2 | docs | `aeat-direct-investigation.md` hand-over text obsolete since 13-10-2026; `unit_suites.md` direct rows stale; verifactu README lacks the direct branch in its source table / diagram and the restore limit | Mark obsolete sections; update | S | verifactu-direct |
| P3 | tests | No global tripwire against real AEAT / gateway hosts in tests | Host allow-list when `LAIDEAL_TEST=1` | S | 00 |
| P3 | verifactu-gateway | Gateway rectificativas without `RectificationItems`; dead tax-item / invoice-type enums; `testConnection` OK on 401 | Auditor decision; remove dead code; parse the body | S-M | verifactu-gateway |
| P3 | verifactu | Unknown estado string falls back to PENDIENTE | Map unknown values to a non-resendable state + warning | S | 00 |
| P3 | tableview / uikit / imprimir / app | Leaks (`new Imprimir` / Listado / RecogPrendas per action, models per refresh), unused includes, colours, `tr()` consistency | Cleanup batch (Haiku / Sonnet) | M | several |
| P3 | repo | Unused icons, stale remote branch, printer 04 / 05 docs Excel-era, milestones size | Hygiene commit | S | 00, printing |
| P3 | printing | qrcodegen not pinned by hash; no third-party notice | README hash + test; notice line | S | printing |

verifactu-direct confirms these points:
- Amount rounding per field is correct: base and cuota rounded separately always add up to the total for one VAT rate on whole cents. Printing F9 verified the same against the renderer.
- WinHTTP validates the server certificate.
- The `AeatStore::append` transaction is sound.

So X4's rounding risk is limited to the gateway path and to Contabilidad's unrounded IVA.
