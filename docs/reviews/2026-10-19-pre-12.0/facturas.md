# Review: facturas (src/facturas/)

## Summary
- Small, clean module (223 lines): UiKit dialog, result panel only, decimal comma, prepared statement, closed-quarter refusal. Healthy for 12.0; no Critical/High bug.
- Main weaknesses are input validation (negative, zero, inf/nan amounts; whitespace-only fields; free-text servicio; no duplicate-invoice check) and thin tests: unit tests cover only the two pure IVA functions, everything else rests on one e2e scenario.
- Doc drifts: two docs still say invoices go to a `facturas` table; module doc says servicio comes from a list but it is not enforced.
- `Facturas::taxBaseFromGross` is also used by the income/Verifactu path (mainwindow.cpp:887, 964): a design coupling worth moving to a neutral helper.

Note on line numbers: facturas.cpp lines below are real file lines.

## Findings

| Sev | Category | file:line | Finding | Proposal | Effort |
|---|---|---|---|---|---|
| Medium ✅ FIXED 20-10-2026 | bug | facturas.cpp:138-141 | Amount validated only with `toDouble(&ok)`. Accepts `inf`, `nan`, `1e3`, negative and zero. `moneyText("inf")` would store `inf`; `0` and `-50` are saved as expenses without a word (a negative gasto lowers deductible IVA in Contabilidad). | Reject non-finite (`std::isfinite`), zero, and decide on negatives (credit notes: allow explicitly with a message, or refuse). Cap at a sane maximum. Same helper for `updateTaxSplit` (facturas.cpp:211) so base/cuota are blank for inf/nan. | S |
| Medium ✅ FIXED 20-10-2026 | bug | facturas.cpp:134-137,186-192 | Only `isEmpty()` is tested on servicio and empresa (not trimmed): whitespace-only servicio passes; servicio/descripcion/empresa are stored untrimmed (only n_factura is trimmed, :187). Servicio is an editable combo and is NOT checked against `servicios` (docs say it comes from that table), so typos create servicios that Listado/GenListado group separately. | Trim all text fields; require servicio to exist in the combo (like empresa, :142) or document that free text is intended. | S |
| Medium ✅ FIXED 20-10-2026 | bug | facturas.cpp:132-150,173-199 | No duplicate-invoice check: same Nº factura + empresa can be entered twice (Enter in a field presses Guardar again after an apparently failed click, or two Facturas windows open, since MainWindow creates a new one each time, mainwindow.cpp:1416). Double-counts IVA soportado. | Refuse (or ask) when `SELECT 1 FROM gastos WHERE n_factura=? AND empresa=?` exists, showing the existing date/amount in the result panel. | S |
| Medium ✅ FIXED 20-10-2026 | design | facturas.cpp:175,184-186 | `id = readMaxValueInColumnFromTable()+1` is a separate open/close from the INSERT. `readMax...` pops a `QMessageBox` on a DB error (sql_lite.cpp:431-439), contradicting "no message boxes" in the module doc; and returns 0 on failure so a failed read would insert id=1 (PK conflict, reported as failed save, ok, but a pop-up appears). | Insert with `id` omitted (INTEGER PRIMARY KEY auto-assigns rowid) or `INSERT ... SELECT COALESCE(MAX(id),0)+1` in one statement; drop the max read. | S |
| Medium ✅ FIXED 20-10-2026 | design | facturas.cpp:182,197 | `db.open()` then unconditional `db.close()`. The connection handle is shared; if a Listado de gastos window is open at the time (its model reads through the live connection, see e2e comment at test_e2e_app.cpp:2766), saving closes the connection under it, and the open Listado does not refresh to show the new row. `quarterIsClosed` already uses the `wasOpen` pattern (sql_lite.cpp:1043). | Use the wasOpen pattern; emit a signal / let MainWindow refresh an open gastos Listado. Ask the Listado reviewer whether it re-opens. | S |
| Low | design | facturas.h:28-29, mainwindow.cpp:887,964 | Income-side tax base (Verifactu BaseImponible) is computed by a static of the expense dialog. A change to the expense math (e.g. rounding) would silently change AEAT records. | Move to a neutral pure helper (sql_lite money helpers or verifactu), keep Facturas calling it; test there. | S |
| Low ✅ FIXED 23-10-2026 | bug | facturas.cpp:201-209 | Base and cuota rounded independently (display and later in Contabilidad: gas21Base = importe/1.21 unrounded, contabilidad.cpp:397). Displayed base+cuota can differ from the total by 1 cent when both fall on .5. Nothing stored, so only the on-screen figure is affected. | Display cuota as `round2(gross) - round2(base)`; add the test below. | S |
| Low ✅ FIXED 26-10-2026 | design | facturas.cpp:65-66 | Rates 21/10/0 hardcoded; Contabilidad hardcodes 1.21 for gastos (contabilidad.cpp:397) while income uses `AppSettings::ivaRate()`. 4 % (superreducido) cannot be entered; rows with other rates are "unrecognised, counted not summed" in Contabilidad. Consistent today, but two lists must change together. | Single shared constant list of expense rates (or note in both headers). | S |
| Low ✅ FIXED 26-10-2026 | bug | facturas.cpp:146 | `quarterIsClosed` checked only at validation; no DB-level guard on INSERT (the Listado edit path has one). Single user, so only a race with an open Contabilidad window. | Optional: re-check inside saveFactura or an `INSERT ... WHERE NOT closedQuarterSql(...)` like the ingresos writers. | M |
| Low ✅ FIXED 26-10-2026 | bug | facturas.cpp:134-150 | No date sanity: any date from 1752 to far future is accepted (typo year 2062 stores a row no report shows). | Warn/refuse when year differs from the current year by more than 1 (panel message). | S |
| Low ✅ FIXED 23-10-2026 | design | facturas.cpp:159 | Success message built before the save from the raw text (`displayMoney(text)`); consistent with stored value, but if validation accepted `inf` it would show it. Covered by the amount fix. | none beyond amount fix. | - |
| Low | docs | see Documentation drift | | | |
| Low | dead-code | facturas.h:25-29 | None found. `taxAmountFromGross` is used only by the dialog and tests (fine). `qDebug` of n_factura/empresa (facturas.cpp:176-181) is acceptable (no secrets). | - | - |

Checked and fine: prepared statement (:184), no SQL concatenation; objectNames present for e2e/UI; `WA_DeleteOnClose`; empresa exact-match check (:142); result panel with HTML escaping of free text (:160-161); decimal comma in/out (`moneyText`/`displayMoney`); closed-quarter refusal incl. empty months (`quarterIsClosed(db, QDate)`); stored date `dd-MM-yyyy`, importe dot with 2 decimals, iva as `21/10/0` string, matching `expensesBetweenDates` and the gastos listing. `edit_lock` is not set on insert (NULL): fine for the test schema (plain TEXT); confirm the shop schema allows NULL (ingresos column is NOT NULL, tests/support/testschema.h:30-33).

## Documentation drift

| Doc (file:line) | Says | Code reality |
|---|---|---|
| docs/ai_agent_instructions.md:112 ✅ FIXED 23-10-2026 | Formal invoice "stored in `facturas` table" | Stored in `gastos` (facturas.cpp:184). No `facturas` table exists. |
| docs/architecture.md:252 ✅ FIXED 23-10-2026 | Tables managed via Listado or Facturas include `facturas`, `empresas` | Only `gastos`, `proveedores`, `servicios` exist for this module; no `facturas`/`empresas` table in the schema. |
| docs/modules/facturas.md:23 (Form fields: servicio "From `servicios` table") + validation list ✅ FIXED 26-10-2026 | Implies servicio comes from the table | Combo is editable and the value is not checked (facturas.cpp:48-52,134-137). Docs should say "any text accepted" or code should enforce. |
| docs/modules/facturas.md:35-39 (Auto-calculation) ✅ FIXED 23-10-2026 | `iva_amount = importe - base` | Code uses `gross*(1-1/(1+r))` (facturas.cpp:205); algebraically equal, only wording. |
| docs/modules/facturas.md "Validation" ✅ FIXED 23-10-2026 | Lists empty/number/empresa/quarter | Omits that negative/zero/inf amounts are currently accepted (state the decision once fixed) and the absence of duplicate detection. |
| docs/testing/unit_suites.md:24,97 and docs/INDEX.md:136 ✅ FIXED 27-10-2026 | `test_facturas`: IVA split | Accurate, but unit_suites.md:97 should list the new tests when added. |
| docs/testing/e2e.md:116 ✅ obsolete 27-10-2026 (row itself said accurate) | Scenario covers split, save, unknown supplier, closed quarter | Accurate (test_e2e_app.cpp:2696-2764); also covers sorted combos (not mentioned in the row). |
| docs/testing/ui_smoke.md:67 (D5) ✅ obsolete 27-10-2026 (row itself said OK) | Expense dated 20-09-2026 refused, today's saved | Matches tests/ui/block_d.py:89-105 via `add_expense` (actions.py:289-305). OK. |
| docs/modules/facturas.md "Key interface" ✅ FIXED 26-10-2026 | `populateEmpresas()`/`populateServicios()` must be called by the caller | True (mainwindow.cpp:1417-1418), but a Facturas built without them has empty lists and every save fails "no está en la lista": consider calling them in the constructor. |

## Test gaps

Current coverage:
- Unit `tests/test_facturas.cpp` (4 tests): IVA split at 21, 10, 0 and reconstruct-gross (100 at 21). Tolerance 0.005 only.
- E2E `test_facturas_saveAndRefusals` (test_e2e_app.cpp:2696): sorted combos; 121,00 at 21 % split shown and saved as `121.00|21`; form cleared; unknown supplier; closed-quarter date; no pop-ups.
- UI smoke: D5 (`add_expense`, block_d.py:89-105) closed-quarter refusal and open-quarter save with real widgets.
- Contabilidad consumes gastos (test_contabilidad: figuresFromDetails, tables) and sql_lite tests cover `quarterIsClosed`.

Untested behaviours and proposed tests:

Unit (pure, guiless; first four need the validation/amount parsing extracted to a static, e.g. `Facturas::parseGross(text, &ok)`):
- `test_taxSplit_roundsToCentsAndKeepsTotal`: for gross in {0.01, 0.10, 0.15, 1.00, 99.99, 1234.57} and rate 21/10, `round2(base)+round2(cuota)` == `round2(gross)` (exposes the 1-cent issue; fix then pins it).
- `test_taxSplit_negativeAndZeroGross`: gross 0 gives 0/0; gross -121 gives -100/-21 (documents the negative decision).
- `test_parseGross_acceptsCommaAndDot_rejectsGarbage`: "121,50"->121.5, "121.50", " 12 ", "" , "abc", "1,2,3", "inf", "nan", "1e3" -> invalid, "-5" per decision.
- `test_taxSplit_largeAmounts`: 999999.99 at 21 keeps 2-decimal consistency.
- `test_validation_*` via QSqlDatabase + testschema (offscreen, like test_settingsdialog): construct `Facturas(db)` in a QTemporaryDir DB (`tests/support/testschema.h`), then:
  - `test_save_storesRowWithDotAmountAndDdMmYyyyDate` ✅ FIXED 26-10-2026 (SELECT every column: fecha `dd-MM-yyyy`, importe `121.00`, iva `21`, trimmed n_factura, id = max+1).
  - `test_save_idContinuesAfterGapAndOnEmptyTable` ✅ FIXED 26-10-2026 (empty gastos -> id 1; ids 1,5 -> 6; text-typed ids).
  - `test_save_incompleteForm_nothingWritten` (each of the four required fields empty in turn; whitespace-only n_factura and importe).
  - `test_save_invalidAmountText_refused` ("abc", "12,3,4").
  - `test_save_supplierMustBeInList_exactMatch` ✅ FIXED 26-10-2026 (case differs -> refused).
  - `test_save_closedQuarterRefused_emptyMonthInClosedQuarter` ✅ FIXED 26-10-2026 (month without gastos but closed quarter, first and last day of quarter, 31-03 vs 01-04 boundary).
  - `test_save_insertFailure_reportedInPanel_formKept` ✅ FIXED 26-10-2026 (e.g. drop gastos table or read-only DB: red panel, form not cleared, no pop-up).
  - `test_save_apostropheInFieldsIsBoundSafely` ✅ FIXED 26-10-2026 (O'Brien supplier, `"; DROP` in descripcion stored verbatim).
  - `test_save_htmlInInvoiceNumberEscapedInPanel` ✅ FIXED 26-10-2026 (`<b>` not rendered).
  - `test_reset_clearsFormAndSplit` ✅ FIXED 26-10-2026 (btnReset: fields, base/cuota empty, IVA back to 21 %, date today, panel "Formulario limpio.").
  - `test_ivaChange_recomputesSplit` ✅ FIXED 26-10-2026 (21 -> 10 -> 0 with importe typed) and `test_invalidImporte_clearsSplit` ✅ FIXED 26-10-2026.
  - `test_populate_sortedSpanish` (also covered by e2e; unit cheaper).
  - `test_duplicateInvoice_refused` (after the duplicate fix).
  - `test_gastoSavedIsPickedUpByContabilidad` ✅ FIXED 26-10-2026: save through the dialog, then `expensesBetweenDates` for the quarter returns it with importe/iva as entered, and `figuresFromDetails` gives base/IVA at 21 and 10 and a 0 % row counted in `gasNiImporte` (closes the producer/consumer loop).

E2E (test_e2e_app.cpp, extend `test_facturas_saveAndRefusals` or add):
- `test_facturas_incompleteAndInvalidAmountInWindow`: empty form message "Formulario incompleto", "abc" amount message, nothing written, no pop-up.
- `test_facturas_savedRowShowsInListadoGastosAndContabilidad`: save, open Listado de gastos (row visible with comma amount `121,00`), generate Contabilidad Trimestral detail for the quarter and see the invoice and IVA soportado 21,00.
- `test_facturas_savedWhileListadoOpen_keepsListadoWorking`: guards the `db.close()` concern above.
- `test_facturas_resetButtonAndSecondSave`: Limpiar, then two saves in a row get ids n and n+1.
- `test_facturas_menuOpensPopulatedDialog`: trigger `actionFormulario_facturas`, assert both combos filled and focus on leFra (the existing test builds the dialog directly, not through MainWindow).
- `test_facturas_enterInInvoiceFieldPressesGuardar`: UiKit Enter rule on leFra/leImporte (incomplete message appears, no duplicate saves).

UI smoke (tests/ui):
- Extend D5 / `add_expense` with a decimal-comma check: type `121,00`, read `leBase` = `100,00`, `leIva` = `21,00` through UIA, and the IVA combo at 10 %.
- New step: the unknown-supplier refusal and the Limpiar button with real widgets (focus returns to Nº factura).
- Check the saved row appears in Ver -> Gastos without reopening the app.

Self-check (`laideal.exe --self-check`, on a copy of the newest backup): add a gastos sanity item, read-only: every gastos row has a parseable `dd-mm-aaaa` date, a dot-decimal `importe` with at most 2 decimals, `iva` in {21,10,0}, unique id, and no duplicate (n_factura, empresa). This is the layer that would catch rows entered by the dialog before the validation fixes (negative/odd rates) and feeds the Contabilidad "unrecognised rate" warning. Effort S-M.

## Questions or cross-module notes
- Contabilidad reviewer: gas21Base uses 1.21 hardcoded (contabilidad.cpp:397) and unrecognised rates are excluded from totals; if negatives/4 % become possible here, confirm the report treats them sanely.
- Listado reviewer: does an open Listado de gastos refresh/survive after Facturas closes the shared connection (facturas.cpp:197)?
- sql_lite reviewer: `readMaxValueInColumnFromTable` shows QMessageBox on error (sql_lite.cpp:431-439), against the no-pop-up rule for windows that use it; also the same db.close() pattern project-wide.
- Verifactu/MainWindow reviewer: income base imponible comes from `Facturas::taxBaseFromGross` (mainwindow.cpp:887,964); rounding of BaseImponible/CuotaRepercutida there should be checked as a compliance item, not only here.
- Open decision for the owner: are negative expense invoices (abonos de proveedor) wanted?
