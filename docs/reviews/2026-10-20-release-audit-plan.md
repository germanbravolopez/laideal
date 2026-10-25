# Release audit 12.0: plan for the open findings (20-10-2026)

The `verifactu-compliance-auditor` reviewed the whole release range `11.2..HEAD` of
`feature/aeat-direct-investigation` (156 commits, 256 files) on 20-10-2026.

**Verdict: READY WITH NOTES.** No blocking findings: the ten requirements of
[verifactu-requirements.md](../modules/verifactu/verifactu-requirements.md) hold, and so do
the gateway paths the shop runs on in 12.0 (gateway in TEST). The branch can go to a PR as
it is. This file plans the non-blocking findings and the doc drift the audit reported.

Known open items the audit agreed do not block: the two AEAT written requests
(`GZ44-2026CTE29840003D`, `GZ44-2026CTE29840004R`), the switch to PRODUCTION, and the
declaración's signature date (at `/release`).

## Plan

One commit per item, each with its test, and each marked ✅ here when it is fixed.

| # | Priority | Req. | File:line | Finding | Planned fix | Status |
|---|----------|------|-----------|---------|-------------|--------|
| 1 | Should fix | Estado / rectificativas | `src/app/rectifyinvoicedialog.cpp:533-539`, `:360` | A gateway PENDING answer (accepted, no CSV) re-enables the form with the ticket still loaded. `onRectifyClicked` does not check `pendingRectificationOf` again, so a second press claims a new number and sends a second rectificativa while AEAT may already hold the first. | Re-check `pendingRectificationOf` (and `holdsPendingCancellation`) in `onRectifyClicked`, or clear `m_loadedTicket` on PENDING as the queued branch does. e2e test: a second press after a PENDING answer sends nothing. | ✅ Fixed 23-10-2026: the PENDING answer clears the loaded ticket and `onRectifyClicked` re-checks `pendingRectificationOf`; `test_rectifyGatewayPendingAnswerSendsNoSecond` |
| 2 | Note (direct client) | 1 / 3 | `src/verifactu/aeatdirectbackend.cpp:699-715` | `verifyStoredChain` never compares each issuer's last stored record with the `aeat_chain` head. Deleting the newest record(s) from `aeat_records` goes undetected until the next record links to the missing huella. | Fail when the stored tail's huella is neither the head nor an anchor. Unit test with the newest record deleted. | ✅ Fixed 23-10-2026: `AeatChainCheck::verifyTail` (the stored tail must be the head unless the head is an anchor), run by `verifyStoredChain`; `test_deletedNewestRecordDetected` |
| 3 | Note | 1 | `src/sql_lite/sql_lite.cpp:109-115` | The identity trigger covers only `ENVIADA`. A bug could still change `n_recibo`, `fecha_pago`, CSV or XML of ANULADA / RECTIFICADA rows: registered invoices that are still exported. | Extend the trigger's `WHEN` to the three estados with `pagado = 'SI'` (the void-date backfill touches only unpaid rows, so it stays compatible). Extend `test_ingresosGuardTriggers`. | ✅ Fixed 23-10-2026: the trigger covers ENVIADA and paid ANULADA / RECTIFICADA rows and is recreated at start; `test_ingresosGuardTriggers` extended |
| 4 | Should fix (needs the gestor) | 2 | `src/app/rectifyinvoicedialog.cpp:368` (older than this branch) | Rectificativas take their numbers from the ticket series. Art. 6.1.a RD 1619/2012 probably requires a separate series for them (the auditor is fairly sure, not certain). | Ask the gestor. If confirmed, give rectificativas their own series (e.g. `R-<n>`) after 12.0, with the numbering, export and QR effects checked. | ✅ Fixed 23-10-2026 (owner's decision, in 12.0): new rectificativas are numbered `R-<n>` (`nextRectificationNumber`, also above AEAT's highest `R-` in production); `test_nextRectificationNumber_ownSeries`, e2e checks the `R-` number |
| 5 | Note | 4 | `src/app/mainwindow.cpp:453-474` | With automatic backups switched off, no pre-migration copy is taken, but `rewriteData` stays true: the data repairs run without a copy of the database as it was. | Take the pre-migration copy whatever the automatic-backup setting, or run only the schema steps when no copy was made. Test. | ✅ Fixed 23-10-2026: the pre-migration copy is taken whatever the automatic-backup setting; `test_mainWindow_preMigrationCopyWithBackupsOff` |
| 6 | Note | 8 (declaración) | `src/verifactu/producerdeclaration.cpp:26-38` | Item j) (the producer's contact address) is the shop's address from Configuración: it changes whenever that setting is edited, while the doc calls it the producer's postal address. | Fix the producer's address in the declaración at `/release`, together with the signature date, instead of reading a runtime setting. | ✅ Accepted by design 23-10-2026 (owner's decision): j) stays the shop's address from Configuración, so no personal address goes into the public repository |
| 7 | Note | 9 | `src/imprimir/imprimir.cpp:407-411` | The leyenda decision reads row 0's estado, while the QR comes from the issue-time result. If the gateway reply's local write fails (an update error or a trigger abort), the factura prints with the QR but without the mandatory text. | When a success QR was passed at issue, print the leyenda with it, or print no QR at all. Test. | ✅ Fixed 23-10-2026: Guardar and Cobrar pass `verifactuIssued` for an accepted reply's QR, so the leyenda prints with it whatever the row's estado (the Imprimir rule is covered by `test_payDialog_directPrintsFacturaBeforeAeatAnswers`'s `verifactuIssued` checks) |
| 8 | Note | Estado | `src/app/mainwindow.cpp:570`, `:646-660` | With the gateway, `settlePendingRectifications` turns an unanswered PENDIENTE rectificativa into ERROR at every start and Configuración save. After a crash in mid-send AEAT may hold it: the resend only gets "duplicate", and for a substitution the original is never marked RECTIFICADA, so Contabilidad counts it twice. | Change the error text from "rehágala" to "compruébela con Consultar en AEAT" (the behaviour is by design). | ✅ Fixed 23-10-2026: the error now says to check it with "Consultar en AEAT" before redoing it |
| 9 | Note | Estado | `src/sql_lite/sql_lite.cpp:1874` (older than this branch) | `NOT IN (...)` on a NULL `verifactu_estado` is NULL, so legacy rows with a NULL estado can never be reconciled from Consultar en AEAT. | `COALESCE(verifactu_estado, '') NOT IN (...)`. Unit test with a NULL-estado row. | ✅ Fixed 23-10-2026: `COALESCE` in the guard; `test_reconcileVerifactuFromAeat_nullEstado` |

## Doc drift to fix for the release

| # | Doc | Says | Correct |
|---|-----|------|---------|
| D1 | `docs/modules/verifactu/verifactu-requirements.md:32` | "A deleted record is detected too" | ✅ Fixed 23-10-2026 with item 2: the doc now names the tail check. |
| D2 | `docs/modules/verifactu/verifactu-requirements.md:111` | The QR "is generated by Irene Solutions from the response" | ✅ Fixed 23-10-2026. Since 12.0 a reprint draws it locally (`AeatQr`) from the stored `verifactu_url_qr`, also with the gateway; `/GetQrCode` only when no URL was stored. |
| D3 | `docs/modules/verifactu/README.md:247`, `:276` | The gateway reprint uses `generateQRAsync` / "regenerated via `/GetQrCode`" | ✅ Fixed 23-10-2026. Same as D2. |
| D4 | `docs/modules/verifactu/README.md:210` | Without configuration "tickets save without an AEAT submission, with `verifactu_estado = "PENDIENTE"`" | ✅ Fixed 23-10-2026. Since 12.0 a paid ticket is refused (`invoicingBlockedReason`). |

Requirements the audit found unaffected by the branch: 5 (traceability), 6 (event log),
7 (export).
