# Review: listado (src/listado/) - before release 12.0

## Summary
- Health: good. Ingresos is read-only in the UI (`NoEditTriggers`), gastos edit/delete/double-click all re-read the closed quarters live, decimals use the comma delegates, UiKit + result panel are used everywhere, no message boxes except the delete confirmation.
- No Critical issue. Main risks: (1) ingresos default order is a TEXT sort of `n_recibo` (wrong "newest first" at digit roll-over) and search/sort only see the 256 rows loaded; (2) ingresos read-only is enforced by a single view setting and has no test at any layer; (3) `GenListado` ignores `writePdf` failure for prendas, computes with `float`, does not HTML-escape and mis-handles IVA other than 21/10.
- Test coverage of the module is thin: only two pure helpers in `test_genlistado.cpp`; the lock/validator behaviour is covered by e2e for gastos and clientes only; prendas/proveedores/servicios add/delete and ingresos read-only are untested.
- Docs are mostly accurate; a few drifts listed below.

## Findings

| Severity | Category | file:line | Finding | Proposal | Effort |
|---|---|---|---|---|---|
| Medium ✅ FIXED 20-10-2026 | bug | listado.cpp:156-159, 199, 297; mysortfilterproxymodel.cpp:168 | `n_recibo` is TEXT (sql_lite.cpp:423 says so; every other query uses `CAST(n_recibo AS INTEGER)`). The ingresos model is sorted in SQL by column 0 as text and the proxy compares column 0 with `localeAwareCompare`: "999" ranks above "1000", "99999" above "100000". "Newest first" and the first 256 loaded rows are wrong at each digit roll-over. | In `lessThan` compare `INGRESOS_COL_N_RECIBO` (and gastos n_factura only if numeric) as integers when both parse; for the SQL batch use `model->setSort` is text-only, so either `setFilter`-free `ORDER BY CAST` via a query model or fetch all rows. Add a unit test (below). | M |
| Medium ✅ FIXED 20-10-2026 | bug | listado.cpp:154-160, 188-192, 263-288 | Ingresos loads lazily (256 rows). The search box (`filterAcceptsRow`) and column sorting only see rows already fetched, so a search for an old ticket/client in Ver > Ingresos shows nothing until the user scrolls. Docs present lazy loading as only a performance choice. | Either fetch all rows when the filter text is non-empty (`while canFetchMore fetchMore` in `textFilterChanged` for ingresos) or document the limit in the intro panel. | S |
| Medium ✅ FIXED 20-10-2026 | compliance (defence in depth) | listado.cpp:236 | Req. 1 (no edit path on invoice rows) rests on one call, `setEditTriggers(NoEditTriggers)`. The `QSqlTableModel` is `OnFieldChange` and the proxy `flags()` still returns `ItemIsEditable` for ingresos (mysortfilterproxymodel.cpp:95-101), so any future `setData` path (a delegate, paste, a test) would write straight to `ingresos`/verifactu columns. No test asserts it. | Make ingresos non-editable at the model level: in `MySortFilterProxyModel::flags` drop `ItemIsEditable` and refuse `setData` when `table_name == "ingresos"`; keep `NoEditTriggers`. Add unit + e2e tests. Run `verifactu-compliance-auditor` on the diff. | S |
| Medium ✅ FIXED 20-10-2026 | bug | genlistado.cpp:150-163; listado.cpp:372-378 | `print_table()` ignores the `bool` of `ReportHtml::writePdf`; Listado always shows "Listado de prendas generado en PDF." (even if the folder is unwritable) and, unlike the gastos listing, gives no link to the file. | Return bool from `print_table()`, show `errorHtml` on failure and `UiKit::fileLinkHtml(file)` on success. Add e2e. | S |
| Medium ✅ FIXED 25-10-2026 | bug | genlistado.cpp:246-251 | IVA handling: only the literal strings "21" and "10" divide out the tax; any other rate (e.g. a free-text "4" or "21.0" typed in Listado de gastos, where the IVA cell is editable without validation) prints base = importe, IVA = 0. Facturas only offers 21/10/0 so this matters for hand edits and legacy rows. | Parse the rate as a number (`toDouble`); validate the IVA and importe cells in the `editValidator` (numeric, IVA in the allowed set). | S |
| Medium ✅ FIXED 25-10-2026 | bug (money) | genlistado.cpp:240-251, 269, 264 | Totals are summed as `float`; with several thousand euros per supplier the single-precision sum can be off by a cent, and `ReportHtml::formatEuro` takes the float widened to double. Contabilidad uses cents/double elsewhere. | Use `double` (or integer cents) for `importe`, base, IVA and `importe_tot`. Unit test on a long sum. | S |
| Low | bug | genlistado.cpp:223-236, 263, 303 | When "Contabilidad cerrada" is chosen the table has 7 columns, but the IMPORTE TOTAL row spans 6+2 = 8: malformed table in the supplier-grouped PDF. | Compute the colspan from the column count. | S |
| Low ✅ FIXED 20-10-2026 | bug | genlistado.cpp:187, 278-281 | Cell texts (prenda name, n_factura, servicio, empresa) are concatenated into HTML unescaped; a supplier containing `<` or `&amp;`-like text breaks or alters the PDF. | `toHtmlEscaped()` every cell (test with `A & B <x>`). | S |
| Low | bug | genlistado.cpp:216 vs 328 | `rowClosed` trims the fecha, the year filter does not (`mid(6,4)` on the raw string). The edit validator (listado.cpp:173) accepts a padded date (" 05-03-2026 ") but the proxy then stores the untrimmed text, so a stored padded date is closed-aware but fails the year filter and the sort. | Trim in one place: have the validator write the trimmed value (e.g. normalise in `MySortFilterProxyModel::setData`) and use `QDate::fromString(...).year()` in `check_years_invoice_type_for_row`. | S |
| Low | bug | genlistado.cpp:152, 209-211 | `model->sort(...)` is called on the Listado's own proxy: generating a PDF reorders the user's visible table and, for prendas, the table is not re-sorted afterwards (indicator out of sync). Within one supplier the order is not by date. | Sort a copy (collect rows into a vector, `std::stable_sort` by empresa then date) or restore the sort after `print_table()`. | M |
| Low | bug | listado.cpp:313-323 | Prendas/proveedores/servicios add: `model()->insertRow(...)` on the proxy and then a raw `INSERT` with empty strings, then `populateTable()` rebuilds the model. The `insertRow` is redundant (and with `OnFieldChange` may itself create a row); the INSERT is positional (`VALUES(?,?,?)`) so depends on column order; a blank-name prenda is stored and sorts first. | Drop the `insertRow` calls; check the bool of `insertNewItemToTable` and report failure; give an unambiguous default name or open a small dialog like `InsertNewItem`. | S |
| Low | bug | listado.cpp:356-360 | Delete: result of `removeRow` is not checked, so "Fila eliminada." shows even if SQLite refused; the confirmation names the row position (`row + 1` of the sorted/filtered view), not its content; delete acts on `currentIndex()` although the selection may have been cleared (handleDoubleClick clears it, line 410) and selection mode is multi-row contiguous. | Check `removeRow`; show the cell text in the question; require a selected row. | S |
| Low | bug | listado.cpp:161-187 | No feedback when a gastos edit is rejected by SQLite (`setData` false from the model, e.g. editing the `id` column, GASTOS_IDX_ID, which is editable and primary key). Non-date columns have no validation (importe "abc" is stored; sort uses `toFloat` = 0). | Make `id` non-editable in `flags`; validate importe/iva numeric in `editValidator`; surface a generic failure from `setData`. | S |
| Low | design | listado.cpp:147-149, 245-248, 367-377, 311-322 | `populateTable()` is re-run after each add/delete/PDF and builds a new `QSqlTableModel`, proxy, delegates and (for gastos) a new `editRefused` connection each time without deleting the old ones (parent = this): memory grows for the life of the window. `GenListado` is `new`-ed per click with parent `this` and never deleted. | Reuse the model (`model->select()`), `delete`/`deleteLater` the old ones, and create `GenListado` on the stack. | M |
| Low | design | listado.cpp:150-151 | Uses `QSqlDatabase::database("qt_sql_default_connection")` and the `db` member side by side; a missing connection leaves `model`/`proxyModel` uninitialised and `resizeWindowToTable()` (line 254) dereferences `table_listado->model()` null. `model`/`proxyModel` public members are not initialised to `nullptr`; `GenListado::model` (genlistado.h:31) is an uninitialised public pointer (null -> crash in `print_table`). | Initialise pointers, early-return, use `db` for the model. | S |
| Low | design | tableview.cpp:8-11 vs listado.cpp:139-141 | The right-click menu always offers "Añadir fila"/"Eliminar fila", even on ingresos (hidden buttons) and gastos Añadir; they answer with a warning. Acceptable but inconsistent with the hidden buttons. | Enable/disable `action1/2` per table in `configureForTable`. | S |
| Low | design | listado.cpp:46 | Window shortcut Ctrl+A is "Actualizar", hijacking the table's Select All. | Use F5 for Actualizar (touches the docs and UI smoke only if they use it). | S |
| Low | security | listado.cpp:29-35 | Click on the QR column opens whatever URL is stored in `verifactu_url_qr` with `QDesktopServices::openUrl`. The value is written by the app, but a restored/edited DB could hold `file:` or other schemes. | Open only `https://` URLs (AEAT host check optional). | S |
| Low | design | listado.h:39-52, 56-62; genlistado.h:31-32, 58 | Guideline deviations: public mutable members (`model`, `tableName`, `table_listado`, `filter_widget`, `lbl_title`, `table_name`) and the private `db` without the `m_` prefix; `closeEvent` declared as a private slot; `C_*` `#define` strings in genlistado.h:11-15 and the `C_NO_ROWS` sentinel string compared in `onGenerateClicked` (line 352). snake_case methods in genlistado.h are the accepted audit exceptions. | Replace the sentinel by an empty string / `std::optional`, `constexpr`/`static const QString`; keep the rest as accepted debt (audit.md:75,78). | S |
| Low | design | listado.cpp:57-63 | Listado's own menu bar is "Archivo / Herramientas" while MainWindow was reorganised by kind of work (Tickets/Facturas/Contabilidad) in ea35285; the "..." rule (e7a6a8c) is not applied (none of these open a window except Generar PDF for gastos). Not documented. | Decide wording; document the menus in listado.md. | S |
| Low | dead-code | tests/ui/actions.py:311-320 | `edit_attempt()` is defined but never used (block_d has its own `gastos_edit`). Candidate to reuse for the ingresos read-only UI step (below), otherwise delete. | Use it in the proposed step E-ingresos or remove. | S |
| Low | docs | see Documentation drift | | | |

## Documentation drift

| Doc (docs/modules/listado.md) | Implementation | Fix |
|---|---|---|
| :5 "a heading ... Añadir fila, Eliminar fila, Generar PDF" | Menus are `Archivo > Actualizar` and `Herramientas > Añadir/Eliminar/Generar` (listado.cpp:57-63); not described. | Add a "Menus and shortcuts" line. |
| :36 "Add / delete rows via the buttons, the menu actions or the table's context menu" | True, but the context menu offers both entries on every table incl. ingresos/gastos where they only warn (tableview.cpp:8-11; listado.cpp:325, 339-342). | Say which tables really add/delete, or fix the menu. |
| :34-41 (Features) | Does not list ingresos presentation: `DecimalCommaDelegate` for size (listado.cpp:214), `LinkDelegate` on the QR column and click-to-open (29-35, 217), `TextColorDelegate` for pagado/estado, hidden columns (hash, edit_lock, csv, xml, cancel_xml, verifactu_hash, seq, invoice_id; 221-232), `placeIngresosDateColumns` (219), gastos hidden `edit_lock` (210). | Add a short "Ingresos view" bullet; cross-link to tableview doc. |
| :40 "`ingresos` ... `setEditTriggers(NoEditTriggers)`" ✅ FIXED 23-10-2026 | Accurate; but the proxy/model remain editable (see finding). The delete/add refusal messages for ingresos (listado.cpp:339-342, 329-331) are not documented. | Update after the hardening. |
| :45-52 Data loading ✅ FIXED 23-10-2026 | Omits that filter and sort act only on loaded rows for ingresos and that sort is textual for `n_recibo`. | Document the limitation or fix it. |
| :54-58 "Dependencies (CMake)" starts with a paragraph about `NumberFormatDelegate` (decimal display) | Misplaced; the CMake text (PUBLIC `tableview`; private `sql_lite`, `appsettings`, `reporthtml`, `uikit`, src/listado/CMakeLists.txt:7-15) is only partly given. | Move the delegate paragraph to Features; list the real link targets. |
| :58 "exposes ... `NumberFormatDelegate`, `TextColorDelegate`" | Also `LinkDelegate`, `DecimalCommaDelegate`, `ingresoscolumns` are used (listado.h:25, cpp:5-7). | Complete the list. |
| :28-29 signals "emitted after editing the clients/garments table" | They are emitted in `closeEvent` only (listado.cpp:385-394), i.e. on closing the window, for clientes and prendas; nothing for proveedores/servicios. | Reword to "when the window closes". |
| :63 InsertNewItem "window-modal" | OK (insertnewitem.cpp:13). | none |
| :66 GenListado | Does not mention the prendas PDF is not linked/verified (finding), the supplier IMPORTE TOTAL rows, the `Cerrado por contabilidad` column, or that the prendas listing sorts the shared proxy. | Add. |
| docs/testing/e2e.md:126 "the Listado lock ... covered by the UI smoke" | The lock is also covered by two e2e tests (e2e.md:118-119). | Reword. |
| docs/testing/unit_suites.md:25,98 ✅ FIXED 23-10-2026 | `test_genlistado` is listed correctly but `MySortFilterProxyModel` lock/validator behaviour has no unit suite entry (none exists). | Update when tests are added. |

## Test gaps

Current coverage map
- `GenListado::filenameSuffix`, `shouldPrintGastoRow`: `tests/test_genlistado.cpp:13-38`.
- Gastos PDF (group by supplier, only year, empty config message, file link): e2e `test_genListadoGastos_pdfAndEmptyYear` (test_e2e_app.cpp:2756).
- Gastos buttons, Añadir message, double-click lock message, flags, delete refused, clientes Añadir via InsertNewItem: e2e `test_listado_buttonsAndMessagesInWindow` (2799).
- Gastos date validator (closed quarter, invalid format, valid date): e2e `test_listado_gastosDateNeverMovesIntoAClosedQuarter` (2851).
- Diacritic filter and `lessThan` for ingresos date/importe/string: `test_mysortfilterproxymodel.cpp:23-125`. Delegates: `test_textcolordelegate.cpp:82-100`.
- UI smoke: B7 (date columns), B8 (SIN COBRAR, accent search, prendas PDF), D5/D6 (gastos lock/unlock), E (delete No).

Not covered (proposed tests)

Unit (`tests/test_mysortfilterproxymodel.cpp`, `tests/test_genlistado.cpp`; model-level, no DB):
- `test_rowLocked_onlyGastosAndClosedQuarter`: closedQuarters={20261}; row dated "15-02-2026" locked, "15-05-2026" not, invalid/empty date not locked; `table_name="prendas"` never locked.
- `test_flags_lockedRowDropsEditable`: locked row lacks `ItemIsEditable`, open row has it.
- `test_flags_ingresosNeverEditable` / `test_setData_ingresosRefused` (after hardening): `flags` has no `ItemIsEditable`, `setData` returns false and the source is unchanged.
- `test_setData_validatorRefusalEmitsEditRefused_andKeepsValue`: validator returns text -> `setData` false, `editRefused` spy count 1, source unchanged; empty text -> written.
- `test_lessThan_nReciboNumeric`: "999" vs "1000" and "99999" vs "100000" (proxy sort descending puts 1000 first) - fails today.
- `test_lessThan_gastosDateInvalidLast`: invalid dates sort deterministically.
- `test_filter_matchesOnlyFirstTenColumns`: a value in column 11 does not match (documents the privacy intent).
- `test_gastosHtml_*` (needs the html builders exposed as static pure helpers, e.g. `GenListado::gastoRowFigures(importe, iva)` and `gastosHtml(rows, options)`): `base_iva_21_10_0_andOther` (121.00/21 -> base 100.00, IVA 21.00; "4" -> 4% split), `rounding_baseplusivaEqualsImporte`, `longSumNoFloatDrift` (10,000 x 0.10 = 1.000,00 exactly), `htmlEscapesCells`, `totalRowColspanMatchesColumnCount` (7 vs 8 columns), `groupedBySupplierTotalsPerSupplier`, `emptyRowsReturnsNoRows`.
- `test_prendasHtml_zeroPricesLeaveCellEmpty`, `test_prendasHtml_commaFormatting`.
- `test_check_years_paddedDate`: " 05-03-2026" counts as 2026.
- `test_storedNumber_rejectsNonNumeric` (if the validator is added), `test_gastosValidator_ivaAllowedSet`.

E2E (`tests/test_e2e_app.cpp`, using the existing `listado()`/`result()` lambdas):
- `test_listado_ingresosIsReadOnly`: open Ingresos with a seeded ENVIADA ticket; `edit triggers == NoEditTriggers`; `model->setData(...)` on importe/estado/verifactu_url_qr returns false (after hardening); `actionEliminar_fila` shows "no se eliminan desde el listado" and COUNT(*) unchanged; `actionAnadir_fila` shows "no admite añadir"; `btnAdd/btnDelete/btnPdf` hidden; hash/xml/csv columns hidden.
- `test_listado_gastosDeleteOpenRow`: delete an open-quarter row (ModalDriver answers Yes) -> row gone from DB and "Fila eliminada."; answer No -> row stays.
- `test_listado_gastosLockIsLive`: open Listado, then `closeQuarter()` from the test, double-click/Delete/setData must be refused without reopening (the `rowLockedNow` claim, listado.cpp:396-402); then reopen the quarter and the row is editable.
- `test_listado_gastosRefusesNonNumericImporte` (after the validator).
- `test_listado_prendasAddEditDelete`, `..._proveedores...`, `..._servicios...`: Añadir -> exactly one new row in the DB; edit price with comma "2,5" -> stored "2.5", shown "2,50"; delete with confirmation; `populatePrendas` spy fires on close (also `populateClientes` for clientes, none for proveedores).
- `test_listado_prendasPdf`: `btnPdf` writes `Listados/Prendas/listado_prendas_<date>.pdf`, `lastReportHtml()` contains names and formatted prices, unwritable folder shows an error (after fix).
- `test_listado_ingresosSearchFindsRowsBeyondFirstBatch`: seed 300 ingresos rows, search the oldest -> row found (after fix); `..._newestFirstAcrossDigitRollover` with tickets 999 and 1000.
- `test_listado_ingresosQrLinkOnlyHttps` (after fix) using a captured `QDesktopServices` URL handler.
- `test_genListado_closedColumnAndTotals`: "Contabilidad cerrada" vs "Incluir todos" columns, "Cerrado por contabilidad" Si/No values.

UI smoke (`tests/ui/block_b.py` / `block_d.py` / `block_e.py`):
- New step B10 "Listado de ingresos is read-only": double-click a cell of a sent ticket (reuse `edit_attempt`, actions.py:311) -> no editor, DB unchanged; Ctrl+D shows the "no se eliminan" message.
- B8 extension: after the prendas PDF, the result panel must carry a link and the file must be non-empty; click Añadir fila on Listado de proveedores and check the blank row appears once.
- D5/D6: also try Eliminar fila on SMOKE-G1 while locked ("Fila bloqueada") and unlocked-after-revert (confirm Yes in a native box).
- Add the Listado de gastos PDF ("Generar PDF" > both groupings) and check `reports/Listados/Gastos/*.pdf` text with `pypdf` (supplier subtotal row present, column count).

Self-check (`laideal.exe --self-check`, src/app/selfcheck.cpp): currently no Listado-related check. Proposals: (a) on the copy of the newest backup, open the `ingresos` model through the same code (`Listado` offscreen) and verify the first row of "newest first" equals `MAX(CAST(n_recibo AS INTEGER))` - catches the text-sort bug on the shop's real data; (b) assert every `gastos.fecha` parses as dd-MM-yyyy and every `gastos.importe`/`iva` is dot-decimal numeric (the data the Listado edit/PDF relies on; legacy comma rows break `toFloat`); (c) report gastos with iva outside {21,10,0}. Effort M.

## Proposals summary (effort)
1. S: numeric `n_recibo` ordering in proxy + decide on lazy loading vs search (two Medium findings).
2. S: model-level read-only for ingresos + tests (Req. 1 hardening) - run compliance auditor.
3. S: GenListado correctness batch (double instead of float, IVA numeric, escape, colspan, writePdf result for prendas, trimmed dates).
4. S/M: validator for gastos importe/iva/id; check `removeRow`/`insertNewItemToTable` results; leave blank-row creation.
5. M: model/proxy lifecycle in `populateTable` (no leaks), `GenListado` on the stack, sort without touching the view.
6. S: docs fixes above; tests listed above (unit S, e2e M, UI smoke M, self-check M).

## Questions or cross-module notes
- tableview reviewer: `MySortFilterProxyModel::lessThan` (mysortfilterproxymodel.cpp:152-170) text-sorts `n_recibo`; `flags()` is editable for ingresos; `filterAcceptsRow` scans only 10 source columns; `TableView` context menu is table-agnostic. Please coordinate on the ingresos read-only hardening and the lessThan fix since both live in `src/tableview/`.
- sql_lite reviewer: `insertNewItemToTable` (sql_lite.cpp:1501-1525) does `db.open()` ... `db.close()` on the shared default connection while Listado's `QSqlTableModel` stays attached to it (works today because the models re-open on use, but it is fragile) and uses a positional `INSERT INTO t VALUES (...)` with no column list, so a schema column added to clientes/prendas/proveedores/servicios would break it silently.
- MainWindow reviewer: `on_actionIngresos/Gastos/..._triggered` (mainwindow.cpp:1262-1340) create a new `Listado` each time with no `WA_DeleteOnClose`, so several stale windows of the same table can be open at once (a gastos edit in one is not seen by another) and none are freed until MainWindow dies.
- Supervisor: the Listado menu bar still uses the old "Herramientas" wording after the menu rework; confirm whether to align before 12.0.
- No Verifactu column is written by this module; the only invoice-related surface is the read-only ingresos view and the URL click. A compliance-auditor pass is only needed if the model-level read-only change is made.
