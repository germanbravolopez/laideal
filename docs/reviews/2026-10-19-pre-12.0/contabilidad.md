# Review: contabilidad (src/contabilidad/) - before release 12.0

Scope read: `src/contabilidad/contabilidad.{h,cpp}`, `docs/modules/contabilidad.md`, accounting parts of `docs/architecture.md`, the sql_lite listing/closure functions it uses (`sql_lite.cpp` 44-90, 865-1200, 1930-2125, 445-475), `reporthtml.cpp:127`, `src/app/selfcheck.cpp:140-180`, `tests/test_contabilidad.cpp`, the three `test_contabilidad_*` scenarios in `tests/test_e2e_app.cpp:2885-3020`, `tests/ui/block_d.py`. Nothing was built or run.

## Summary

- Health: good. Figures all derive from one set of detail rows, `figuresFromDetails` is pure and well tested, the "never close without the report" rule is honoured and e2e-tested, the closure history is append-only.
- Main risks are not arithmetic but integrity: the row collectors swallow SQL errors (a failed query yields an empty report that can then LOCK the quarter), the gastos IVA text parses silently to "sin IVA", and a closed quarter's filed PDF is deleted and rewritten on regeneration.
- IVA is computed from unrounded sums while Verifactu/AEAT records are rounded per invoice, so the quarter's IVA repercutido can differ by cents from the sum of the quotas registered at AEAT.
- Tests: pure logic is well covered; the dialog flow (Mensual/Anual generate, no-movements, close failure, comma warning, config toggling) is thinly covered; Anual/Mensual have only a "non-empty" UI check.
- Docs: about a dozen small drifts (stale `.ui`/`cb_config`/"Bloquear datos" names, "error dialog" vs result panel, `figuresFromDetails` signature, "All pure statics, no DB").

## Findings

| Severity | Category | file:line | Finding | Proposal | Effort |
|---|---|---|---|---|---|
| High ✅ FIXED 19-10-2026 | bug/compliance | `sql_lite.cpp:1951-1957, 1994-1999, 2036-2041` (collectors) used from `contabilidad.cpp:289-291, 315, 232-243` | `collectIncomeTickets/Expenses/Regularizations` on `q.exec()` failure (or `db.open()` failure, unchecked) only `qWarning` and return an empty vector. `generateContabilidad` cannot tell "no rows" from "query failed", writes a report with zero figures and, with Bloquear ticked, `closeQuarter` locks the quarter with that wrong report. `quarterHasMovements` is a separate query and can succeed while the collectors fail (locked file, corrupt DB). | Make the three public listings return a success flag (out-param or `std::optional`/struct with `ok`), and have `generateContabilidad` return empty file + an error message ("No se pudieron leer los datos") so the existing "not closed" path applies. Also check `db.open()`. Test: DB with a dropped `gastos` table, Trimestral + Bloquear must not close. (Opus: touches close of a legal period.) | M |
| Medium ✅ FIXED 19-10-2026 | bug/compliance | `sql_lite.cpp:2000-2016` (`e.iva = value(4).toInt()`), `contabilidad.cpp:417-421` | `gastos.iva` is a TEXT column (`testschema.h:32`). A value that is not a clean integer ("21,0", "21.0", "21 %", "") makes `toInt()` return 0 and the row is summed as SIN IVA (iva 0): IVA soportado silently understated, no flag. Only a SQL NULL is protected (-1). | Parse with `toInt(&ok)` (and accept "21.0" via `toDouble`), map failure to -1 so the existing "tipo de IVA no reconocido" flag fires. Test in test_sql_lite with iva values "21.0", "", "abc", "21,5". | S |
| Medium ✅ FIXED 19-10-2026 | compliance/design | `contabilidad.cpp:114-115, 193-251` | Nothing stops closing a quarter that has not ended (current year quarter selectable up to Q4; today is inside Q4 2026). Locking the quarter that contains today makes `kUnlockedWhere` block every payment dated today, i.e. the shop cannot Cobrar until Revertir. No ordering check either (Q2 can be closed while Q1 is open, or Q1 reverted while Q2 is closed - the regularisation dates then land in `openPeriodDate`). | When Bloquear is ticked and `endExclusive > today` show a warning in the result panel and require a second click (or refuse); consider warning when an earlier quarter of the same year is open. Unit: static `periodHasEnded(mode, unit, year, today)`. | S |
| Medium ✅ FIXED 20-10-2026 | compliance | `contabilidad.cpp:222-227`, `reporthtml.cpp:136` | Regenerating an already closed quarter (allowed by design) deletes the existing PDF first (`QFile::remove` in `writePdf`) and writes the new one. The filed document is replaced by whatever today's code produces (a release that changes a figure, an `ivaRate` change, an added regularisation line) with no trace, and if the write fails the filed PDF is already gone. | For a closed quarter write to a new name (`..._YYYYMMDD_regenerado.pdf`) or keep the original and only add; at minimum write to a temp file and move over the old one only on success. Record the regenerated file in the log. | M |
| Medium ✅ FIXED 19-10-2026 | compliance | `contabilidad.cpp:390-391, 610, 656`; `aeatrecord.cpp:192-193` | Report IVA = (sum of gross) - (sum of gross)/1.21, unrounded. Verifactu records carry base and quota rounded to 2 decimals per invoice (per payment event). Over a quarter the sum of registered `CuotaRepercutida` can differ from the report's "IVA repercutido" by cents (random walk, worst case N x 0.005). The modelo 303 figure should match what was registered at AEAT. | Decide the rule with the accountant: compute base/quota per event rounded (same helper the Verifactu build uses, `Facturas::taxBaseFromGross`) and sum the rounded values; keep the "rounded per line" footnote true. Add a test that report IVA == sum of per-record quotas for 200 generated amounts. (Opus + compliance auditor.) | M |
| Medium ✅ FIXED 19-10-2026 | bug | `contabilidad.cpp:255-258`, `figuresFromDetails:417-421`, `createHtmlDetailGastos:714` | Gastos with an unrecognised/NULL IVA rate are left out of the sums and are flagged only inside the optional detail annex. The summary-only PDF and the result panel give no warning, so a 4 % invoice or a mistyped rate vanishes from IVA soportado unnoticed (invalid comma amounts do get the red message, rates do not). | Count them (`unrecognisedIvaCount`) and add a warning to the result panel and a line under the Gastos summary table, like `invalidAmounts`. | S |
| Low ✅ FIXED 26-10-2026 | bug | `sql_lite.cpp:2022, 1985` | Non-numeric `importe` without a comma ("abc", "12,50 EUR" contains a comma so is flagged, "12.5e", "") yields `toDouble() == 0` silently. Only the comma case is flagged. | `toDouble(&ok)`; count `!ok` and non-empty as invalid amount. | S |
| Low ✅ FIXED 23-10-2026 | bug | `sql_lite.cpp:1925-2090` (collectors) | The collectors call `db.open()`/`db.close()` unconditionally instead of the `wasOpen` pattern used by `quarterIsClosed` etc. (`sql_lite.cpp:1041-1060`). If the caller (or Listado) holds the connection open, the report closes it from under them. Contabilidad works today only because MainWindow's handle is closed between calls. Cross-module (sql_lite). | Adopt the `wasOpen` pattern in the three collectors. | S |
| Low ✅ FIXED 27-10-2026 (owner's decision) | bug | `contabilidad.cpp:445-447` | Regularisations with an empty/invalid `fechaPago` are skipped in `netTicketCount` (not offset), so such a ticket stays counted while its money is netted (counts and amounts disagree). Likely only legacy rows. | Treat invalid payment date as "in this period" (money is netted, so count must be too) or flag. Test with empty fechaPago. | S |
| Low ✅ FIXED 26-10-2026 | bug | `sql_lite.cpp:1025-1029 vs 1031-1033` | Income predicate lets a paid row with any non-empty `fecha_anulacion` through, but the regularisation predicate requires estado ANULADA/RECTIFICADA. A row with `fecha_anulacion` set and another estado (e.g. restored/ENVIADA) is counted as income and never subtracted - consistent with "kept", but nothing is subtracted if estado was later reset. Low probability. | Add a sql_lite test that pins this combination (documented decision) or align the predicates. | S |
| Low ✅ FIXED 26-10-2026 | bug | `contabilidad.cpp:113-115` | Year lower bound uses `MIN(substr(fecha_recepcion,7,4))`; a single row with `''` (not NULL) collapses the range to the current year (the very failure the comment says it avoids, for fecha_pago). Also ignores gastos dated before the first ingreso. `readMaxNMinYearInColumnFromTable` pops a `QMessageBox` (sql_lite.cpp:463, 470) from the constructor. | `MIN(NULLIF(...))` / filter `GLOB '[0-9][0-9]-[0-9][0-9]-[0-9][0-9][0-9][0-9]'`, include `gastos.fecha`, no message box from the dialog constructor. | S |
| Low ✅ FIXED 26-10-2026 | bug | `contabilidad.cpp:156-170, 168-169` | `lockStatusMessage` when `quarterClosures` or `quarterHasMovements` query fails returns "no tiene registros" (both fail to empty/false) while `quarterIsClosed` fails closed ("bloqueado"). Mensual and Trimestral can disagree after a DB error. | Surface a read error in the message, or reuse a fail-closed result. | S |
| Low ✅ FIXED 27-10-2026 (owner's decision) | design | `contabilidad.cpp:210-221` | Revertir has no confirmation and logs only `qDebug`. A mis-click on Revertir reopens a filed quarter at once (the history row survives, so it is recoverable, but the shop does not know the quarter is now editable). | One confirm step in the result panel (button text "Confirmar reversión") or a tracker/log line via `AppLogger` at warning level. | S |
| Low | design | `contabilidad.h:36-37`, `mainwindow.cpp:1404-1409` | Revert mode is a public field plus `resetAllContents()`; forgetting the second call gives a revert dialog with Generar UI (e2e does it correctly, callers must too). | Constructor argument or `static Contabilidad *forRevert(db, parent)`. | S |
| Low | design | `contabilidad.cpp:203-208` | Mensual/Anual with Bloquear impossible (greyed) but `onGenerateClicked` for Mensual/Anual in revert mode cannot occur (combo disabled); no defensive guard if `revertirOn` and mode != Trimestral. Fine today. | None needed; add a Q_ASSERT-style log branch only if the combo logic changes. | - |
| Low ✅ FIXED 26-10-2026 | dead-code | `contabilidad.h:6, 11` ; `contabilidad.cpp:371` | `#include <QDesktopServices>` and `<QSet>` in the header are not used by the header (QSet is needed only by the .cpp); `periodRange(int)` comment mentions `sb_trim`. `figuresFromTotals` is only called by `figuresFromDetails` (fine, but could be inlined). No dead public methods found. | Trim includes, fix comment. | S |
| Low ✅ FIXED 26-10-2026 | unit-tests | `tests/test_contabilidad.cpp:1-3` | File header says it tests `periodRangeFor` only; the suite also uses a real throwaway DB (`TestSchema`, `closeQuarter`) at 110-165. | Update header; see doc drift. | S |

## Documentation drift

| Doc location | Says | Code/test reality |
|---|---|---|
| `docs/modules/contabilidad.md:48` ✅ FIXED 26-10-2026 | `figuresFromDetails(income, expenses, ivaRate)` | `contabilidad.h:108-112`: also takes `regularizations`, `periodStart`, `periodEnd` |
| `docs/modules/contabilidad.md:59` ✅ FIXED 26-10-2026 | "`generateContabilidad()` shows one error dialog per report pointing at the decimal clean-up tool" | `contabilidad.cpp:255-258`: red text in the result panel, no dialog (md:17 itself says no pop-ups; e2e asserts `m_popups` empty) |
| `docs/modules/contabilidad.md:23` and `contabilidad.h:29-31` ✅ FIXED 26-10-2026 | combobox `cb_config` "(see contabilidad.ui)" | No `.ui`; object name `cbConfig` (`contabilidad.cpp:51-53`) |
| `contabilidad.h:45-46` ✅ FIXED 26-10-2026 | "Bloquear datos" checkbox | Label is "Bloquear el trimestre al generar la contabilidad" (`contabilidad.cpp:70`) |
| `contabilidad.cpp:371` ✅ FIXED 26-10-2026 | "Mensual reads the month from sb_trim" | Widget is `m_sbPeriod` / `sbPeriod` |
| `docs/modules/contabilidad.md:57` ✅ FIXED 26-10-2026 | period range and the four-way switch "both flow through `periodRange()`" | Annual goes through `periodRangeFor` directly (`contabilidad.cpp:321`) |
| `docs/modules/contabilidad.md:68` ✅ FIXED 26-10-2026 | Detail annex has two tables and "both tables end with a note" | Three tables: `createHtmlDetailRegularizaciones` (`contabilidad.cpp:636`) also ends with the note; md:68 only mentions it in the regularisation section (83) |
| `docs/modules/contabilidad.md:91` ✅ FIXED 26-10-2026 | "Both detail listings use a half-open interval" | Three listings (regularisations use `fecha_anulacion`, `sql_lite.cpp:1031-1033`) |
| `docs/modules/contabilidad.md:72` ✅ FIXED 26-10-2026 | report "opened automatically via `QDesktopServices::openUrl()`" | Done inside `ReportHtml::writePdf` (`reporthtml.cpp:145`, switchable), plus a link in the result panel (`contabilidad.cpp:253-254`) |
| `docs/modules/contabilidad.md` (no mention) ✅ FIXED 26-10-2026 | - | Mensual subtitle "Contabilidad cerrada/no cerrada" (`contabilidad.cpp:299-303`) and per-quarter closed headings in the Anual report (`:319-327`) are undocumented |
| `docs/architecture.md:84, 87` ✅ FIXED 26-10-2026 | "Counts are the number of detail rows (distinct paid tickets, gastos rows)"; income listing "excludes ANULADA/RECTIFICADA" | Count is `netTicketCount` (netted by regularisations); rows with `fecha_anulacion` are kept as income and subtracted elsewhere (md:76-85). Architecture paragraph is a duplicate of the module doc and stale; replace by a link |
| `docs/architecture.md:86` ✅ FIXED 26-10-2026 | Detalle lists "paid tickets and the gastos rows" | Also the regularisations table |
| `docs/testing/unit_suites.md:51` | "All pure statics, no DB" | `test_contabilidad.cpp:110-165` create a DB and call `closeQuarter/reopenQuarter/migrateDatabase` |
| `docs/testing/unit_suites.md:49-57` | Lists coverage of Locks, Figures, Counts, Tables | Does not mention `invalidAmountCount` (covered at test:319) - fine; but the doc does not state the untested dialog flow (see gaps) |
| `docs/testing/e2e.md:62` | Mentions the 10.12 count bug found by e2e | Consistent; no drift |

## Test gaps

Behaviour -> existing coverage:
- `periodRangeFor` (Q1-Q4, months, Dec roll-over, annual units): `test_trimestralQuarters`, `test_mensualMonths`, `test_anualUsesQuarterUnit`.
- `reportRelativePath`: `test_reportRelativePath`. `lockOptionAvailable`: `test_lockOptionOnlyForTrimestral`.
- `lockStatusMessage` (Trimestral/Mensual/Anual, migrated): `test_lockStatusMessage_*`.
- `figuresFromDetails` (21 % split, gastos buckets, unrecognised/NULL IVA, regularisation net): `test_figuresFromDetails*`.
- `netTicketCount/yearTicketCount/invalidAmountCount`: `test_netTicketCount`, `test_yearTicket*`, `test_invalidAmountsFlagged`.
- Detail HTML (ingresos/gastos/regularisations/empty/rounding note): `test_detail*`, `test_gastosTable_columnsAndSubtotal`.
- Dialog flow: e2e `generateLockThenRevert` (Q1 generate+lock, check, detail, regenerate-when-closed, revert, revert-twice), `unwrittenReportDoesNotLock`, `quarterWithOnlyACancellation`.
- SQL listings/closures/hasMovements: `test_sql_lite.cpp` (149-420, 555-565).

Untested behaviours and proposed tests:

Unit (`tests/test_contabilidad.cpp`, pure):
- `test_summaryLabels_signs`: `createHtmlSummary` is private/untested - make it a public static or test via `renderSection`; with ingIva < gastosIva assert "a compensar" and "(p&eacute;rdida)", with the reverse "a ingresar"/"(beneficio)".
- `test_tableIngresos_showsRegularisationRows`: `f.ingRegularizacion > 0` produces the three rows "Ingresos del periodo", "Anulaciones / rectificaciones", net "Importe total" with `-regularisation`; with 0 no such rows.
- `test_figuresFromDetails_zeroAndNegativeNet` ✅ FIXED 26-10-2026: regularisation greater than income -> `ingImporte < 0`, base/IVA negative, resultadoIva negative (a compensar), no NaN.
- `test_figuresFromDetails_ivaRateOtherThan21` ✅ FIXED 26-10-2026: `ivaRate=10` base/quota.
- `test_netTicketCount_emptyOrInvalidFechaPago` (see Low finding), `test_netTicketCount_multiPaymentTicketAcrossQuarters` (A paid Q1, B paid Q2, both cancelled Q2: Q2 not counted, Q1 counted).
- `test_invalidAmountCount_regularizations` ✅ FIXED 26-10-2026: regularisation with `invalidAmounts` counted.
- `test_detailIngresos_totalsEqualSummary`: for 50 random cent amounts, total row == `figuresFromDetails` ingImporte/ingBase (rounding claim in `kRoundingNote`).
- `test_detailRegularizaciones_flaggedAndEscaping` ✅ FIXED 26-10-2026: `*` marker footnote and `toHtmlEscaped` of cliente.
- `test_expenseIvaParsing` (needs finding #2 fix): in test_sql_lite, "21.0", "", "abc" -> iva -1.
- `test_collectors_failureReported` (needs finding #1 fix).
- `test_periodHasEnded` (needs the future-quarter guard).
- `test_rounding_matchesVerifactuQuotas` (if the rounding rule changes).

Test seam: `onGenerateClicked` branches need the dialog; they belong in e2e below.

E2E (`tests/test_e2e_app.cpp`, Contabilidad scenarios):
- `test_contabilidad_mensualReport`: Mensual month 2, assert path `Mensual/reporte_mensual_2026_2.pdf`, subtitle "Periodo: 01-02-2026 a 28-02-2026" and "Contabilidad cerrada" after closing Q1, lock box disabled and unticked after switching from Trimestral+ticked to Mensual.
- `test_contabilidad_anualReport`: seed Q1 and Q3 rows plus a cancellation; assert four "Trimestre N" headings with closed/not closed, consolidated total == sum, distinct year ticket count for a ticket paid Q1/Q2, detail variant `_detalle`.
- `test_contabilidad_noMovementsWarns`: empty quarter -> amber "No hay registros", no PDF, no closure.
- `test_contabilidad_commaAmountWarning`: seed a ticket with `importe='10,50'` -> red message with the count, row marked `*` in detail, closure still allowed (decide), figures exclude it.
- `test_contabilidad_closeFailureAfterReport`: make `closeQuarter` fail (pre-insert an open closure via a second path / read-only trigger) -> "realizada" + "No se pudo bloquear".
- `test_contabilidad_revertOnOpenQuarter`: warns "nada que revertir" (partly covered at the end of generateLockThenRevert; keep) and a revert of a quarter that is not selectable year.
- `test_contabilidad_regenerateKeepsFiledPdf` (after finding #4 is fixed).
- `test_contabilidad_lockFutureQuarterWarns` (after finding #3).
- `test_contabilidad_checkLock_annualAndMensual`: Comprobar bloqueo from the widgets in Mensual and Anual (statics tested, widget wiring not).
- `test_contabilidad_yearRangeFromData`: `sbYear` minimum is the oldest `fecha_recepcion` year; with a `''` row, range still correct.

UI smoke (`tests/ui/block_d.py`):
- D3 only asserts the annual PDF is non-empty (`block_d.py:62`): add assertions for the Q3 66.30 / 60.50 / Q4 -12.10 totals in the consolidated block and the "Contabilidad cerrada" heading of Q3 after D5 (reorder: annual after the lock).
- D7 Mensual report for the month with the smoke tickets: path under `Mensual/`, income equals the month's tickets.
- D8 regenerate a closed quarter: result says "ya estaba realizada", PDF still present.
- D9 empty quarter: amber "No hay registros".
- D10 year spinner lower bound equals the smoke DB's first year (catches the `''` regression).
- D6 also check that Cobrar of a ticket dated in the reverted quarter works again (income side of the lock), not only the gastos edit.

Self-check (`src/app/selfcheck.cpp:150-175`):
- It recomputes the last closed quarter but never compares: store the figures (or a hash of the figure tuple) in `accounting_closures` at close time (new nullable columns `income_cents`, `iva_cents`, `report_hash`; migration - Opus + compliance), then self-check M2b flags "las cifras del trimestre cerrado han cambiado desde que se presento". This is the only automated detector of a code/data change moving a filed quarter. Effort M.
- Cheaper: self-check asserts `facts.reportFile` exists and is non-empty (already does), and that every closed quarter in the table has `report_file` present in the Contabilidad folder (warning only). Effort S.
- Self-check for the collectors: a deliberate unreadable `gastos` table in the DB copy must fail the check instead of reporting zeros (needs finding #1 fix).

## Questions or cross-module notes

- sql_lite reviewer: the collectors (`sql_lite.cpp:1930-2090`) are the heart of the report; error propagation (finding 1), `wasOpen` pattern (finding Low), iva/importe parsing (findings 2 and Low) belong to that module but need a contract change here. Suggest a result struct `{ok, rows}` and I will adapt the Contabilidad side.
- reporthtml reviewer: `writePdf` removes the target before writing (`reporthtml.cpp:136`) and returns only bool; a temp-file-then-rename option is needed for the closed-quarter regeneration finding. Also `writePdf` opens the PDF by default (`s_openGeneratedReports`), fine for the doc fix.
- Verifactu/compliance: per-invoice rounding vs the report's unrounded IVA (finding Medium) needs a decision by the supervisor/accountant - what must the modelo 303 figure equal: sum of registered quotas, or recomputed from gross?
- Supervisor: ordering policy for closing quarters (future quarter, earlier quarter open, reverting an earlier quarter while a later one is closed) interacts with `openPeriodDate` and the Cancel/Rectify dialogs; it is a product rule, not a code detail.
- Assumption worth confirming: pre-10.12 cancelled invoices without `fecha_anulacion` remain excluded from their own period (md:85); no regularisation is ever created for them, which is correct only if their quarters were not already filed with the invoice included.
