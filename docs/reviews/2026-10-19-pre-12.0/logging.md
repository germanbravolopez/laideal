# Review: logging (src/logging/) - release 12.0

## Summary
- AppLogger is 61 lines, simple and works, but has zero tests and several robustness gaps (rotation only at startup, `.old` removed before rename, no previous-handler chaining, silent failure when the file cannot be opened).
- Privacy: ServiceKey is redacted (verifactumanager.cpp:86-91) and no certificate password/token is logged. But client name + phones + garment data are logged in clear (add_garment.cpp:322, sql_lite.cpp:589/597/607/619/1447) and the log is meant to be sent to support.
- Docs mostly match; drifts: the "Abrir archivo" button (architecture.md:166, INDEX.md:184) does not exist (it is a clickable link); rotation is described as in-session but is startup-only.
- Health: acceptable for a VERIFACTU-mode app (Req. 6 is informational); fix rotation/robustness and PII items before 12.0 if cheap.

## Findings

| Sev | Cat | file:line | Finding | Proposal | Effort |
|---|---|---|---|---|---|
| Medium ✅ FIXED 19-10-2026 | security | src/add_garment/add_garment.cpp:322-324 | Every inserted garment logs `cliente=`, prenda, importe, hash in clear. The log is sent to support (mainwindow.cpp:1483). | Log only ticket number, count, importe; drop `cliente`. | S |
| Medium ✅ FIXED 19-10-2026 | security | src/sql_lite/sql_lite.cpp:589,597,607-608,619,1447 | Client name, `tel_fijo`, `movil` logged in clear (addNewClient, updateItemToClient, readClientPhones). Phones are personal data (RGPD). | Remove name/phones from messages; keep SQL error text only. | S |
| Medium ✅ FIXED 20-10-2026 | bug | src/logging/applogger.cpp:42-47 | Rotation is checked only at startup. A shop keeping the app open for weeks (279 qDebug/qWarning sites, payload dumps) can exceed 5 MB without bound; docs say "rotates when the file exceeds 5 MB". | Track bytes written in the handler and rotate under the mutex (close, rename, reopen) when > 5 MB. | M |
| Medium ✅ FIXED 20-10-2026 | bug | src/logging/applogger.cpp:45-46 | `QFile::remove(.old)` then `rename`, return values ignored. If rename fails (file open elsewhere, e.g. Notepad or the UI-test reader on Windows) the log keeps growing and the previous `.old` is already lost. | Check results; on failure keep appending or truncate; remove `.old` only when rename can proceed. | S |
| Medium ✅ FIXED 20-10-2026 | bug | src/logging/applogger.cpp:50-52 | If the log cannot be opened (read-only/locked home, redirected profile) `install()` returns silently, no handler is installed (messages go to invisible stderr) and Ayuda -> Log shows a path to a non-existent file. | Fall back to `QStandardPaths::TempLocation`; make `on_actionMostrar_log_triggered` (mainwindow.cpp:1479) say "no log file" when `!QFile::exists`. | S |
| Medium | bug | src/logging/applogger.cpp:60 | Return value of `qInstallMessageHandler` (previous handler) is discarded: no chaining, no stderr echo, so a debug run (VSCode F5) shows nothing in the console. Tests never call `install()`, so the handler is never exercised. | Keep and call the previous handler (or echo to stderr in debug builds). | S |
| Low | bug | src/logging/applogger.cpp:12-14 | Static QFile/QTextStream are destroyed at exit while other threads (QNetworkAccessManager, updater) may still log; handler would use a destroyed stream. No flush/uninstall on `aboutToQuit`. | `AppLogger::shutdown()` on `aboutToQuit` resetting the handler under the mutex. | S |
| Low | bug | src/logging/applogger.cpp:19-23,30-31 | `QtInfoMsg` is tagged DEBUG; explicit `abort()` after fatal is redundant (Qt aborts) but harmless. | Add `[INFO]` tag; comment or drop abort(). | S |
| Low | design | src/logging/applogger.cpp:25-27 | Second-resolution timestamps, no thread id/function: ordering of submit/reply bursts is ambiguous. | `yyyy-MM-dd HH:mm:ss.zzz`; optionally `context.function` for WARN/CRIT. | S |
| Low | design | src/logging/applogger.cpp:37 | Path fixed to `QDir::homePath()`; no seam, so UI tests append to the developer's real log (ui_smoke.md:58) and unit tests cannot test the module. | Env var `LAIDEAL_LOG` (or `installAt(path)`) as test seam. | S |
| Low | security | src/logging/applogger.cpp:25-27 | No redaction layer; only verifactumanager.cpp:91 redacts ServiceKey by hand. A future `qDebug() << payload/settings` would leak. | Central scrubber in the handler for `ServiceKey`/`password`/`Clave` values. | M |
| Low | compliance | src/verifactu/verifactumanager.cpp:91,334,355 | Legacy Irene Solutions path logs the full invoice JSON (NIF and customer fields) and the full server response. | Confirm whether VerifactuManager is still active in 12.0; if so log invoice number + totals only. | S |
| Low | compliance | src/verifactu/aeatdirectbackend.cpp:446-449,618,682; aeatstore.cpp:391 | Direct backend logs record id, invoice number, huella, endpoint, state, CSV, SOAP fault: good, no secrets, no certificate path/password found. Missing: one line per record outcome (accepted/rejected + AEAT error code), the key diagnostic for refused invoices. | Add a qDebug per record outcome. | S |
| Low ✅ FIXED 20-10-2026 | unit-tests | tests/ (none) | No test mentions AppLogger or ~/.laideal.log. | See Test gaps. | M |

No dead code in the module (CMakeLists fine).

## Documentation drift
- docs/architecture.md:166 and docs/INDEX.md:184: "Abrir archivo" button that opens the log; code (src/app/mainwindow.cpp:1481-1484) shows a result-panel `UiKit::fileLinkHtml` link ("clic para abrirlo"). Fix wording.
- docs/architecture.md:161: lists qDebug/qWarning/qCritical/qFatal; qInfo is also redirected (tagged DEBUG, applogger.cpp:19-23).
- docs/architecture.md:165 and docs/modules/verifactu/verifactu-requirements.md:76: rotation "at 5 MB" is true only at launch (applogger.cpp:42-47); only one `.old` generation is kept (not documented).
- docs/architecture.md:161 "No changes required at any call site" - add the rule of what must never be logged (service key, certificate password, client names/phones) here and in /coding-guidelines.
- docs/testing/ui_smoke.md:58 and tests/ui/laideal_ui.py:215-221: the session log is read from a start offset; this breaks if the log rotates during the session (offset beyond the new size).

## Test gaps
Existing: none for AppLogger. Indirect: test_selfcheck exercises the chained LogCapture (selfcheck.cpp:21-34) with the default handler; UI scripts read the log.

Unit (new `test_applogger`, needs a path seam):
- `install_createsFileAndWritesSessionHeader` - file exists, contains "=== Session started:".
- `handler_formatsLevelTags` - lines match `^\d{4}-\d\d-\d\d \d\d:\d\d:\d\d \[(DEBUG|WARN|CRIT)\] msg$`.
- `handler_writesUtf8` - "camisa ñ á €" round-trips.
- `rotation_movesBigFileToOld` - 6 MB pre-existing file -> `.old` has 6 MB, new file small.
- `rotation_replacesExistingOld`.
- `rotation_inSession` (after implementing) - size bounded after >5 MB written.
- `install_unwritablePath_doesNotCrash`.
- `concurrentWrites_noInterleaving` - 4 threads x 500 lines, 2000 intact lines.
- `previousHandlerIsChained` (after implementing).
- Privacy regressions with a capturing handler: `addNewClient_doesNotLogPhones`, `insertGarment_doesNotLogClientName`, `submitInvoice_payloadLogRedactsServiceKey`.

E2E (test_e2e_app / test_e2e_verifactu): capture messages through a full save + fake AEAT submit; assert no message contains the service key, certificate password, or the fixture client's phone; assert one message carries invoice number, record id and AEAT state.

UI smoke: add `no_secrets_in_log` check over `Run.log()` (synthetic client name/phone absent); Ayuda -> Log de depuración shows a link containing `.laideal.log`; exactly one session header per launch.

Self-check: add "log writable" check (append open, size, warn > 5 MB or `.old` locked); report count of WARN/CRIT lines of the session without content.

## Questions / cross-module notes
- Verifactu reviewer: is `VerifactuManager` (legacy) still reachable in 12.0? It logs full payload/response (verifactumanager.cpp:91,334,355).
- sql_lite / add_garment reviewers: PII in qDebug lines listed above.
- selfcheck reviewer: the report is documented as free of shop data; make sure it never embeds log lines.
- Supervisor: consider "Copiar log reciente" (last ~500 lines) in the Ayuda action to ease support (S/M).
