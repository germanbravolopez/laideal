# Review: tableview (src/tableview/) - release 12.0

## Summary
- Small module (544 lines), mostly sound; tests pin the happy paths (diacritics, 3 comparators, classify, column placement, decimal delegates).
- Two real sort bugs: `n_recibo` (TEXT) sorts as a string (the default sort of Recogida and Listado ingresos) and `fecha_anulacion` is missing from the date comparator.
- The filter scans only the first 10 source columns (Serv./Tam./Obs. visible but unsearchable; docs say "all columns"), ignores "Distinguir mayusculas" (regex OR normalized), and runs twice per keystroke in Listado.
- Docs drift in architecture.md, listado.md, recog_prendas.md; no tests for rowLocked/flags/editValidator/filter column/regex path/FilterWidget/LinkDelegate/TableView; no e2e/UI scenario sorts or filters a grid.
- Health: yellow. Nothing compliance-critical, but "newest receipt first" breaks at the n_recibo digit rollover.

## Findings

| Sev | Cat | file:line | Finding | Proposal | Effort |
|---|---|---|---|---|---|
| High ✅ FIXED 20-10-2026 | bug | src/tableview/mysortfilterproxymodel.cpp:97-108 (n_recibo is TEXT: src/sql_lite/sql_lite.cpp:423 comment) | `lessThan` has no numeric branch for ingresos column 0 (`INGRESOS_COL_N_RECIBO`), so it falls to `localeAwareCompare` ("99999" > "100000"). Listado ingresos and Recogida both call `sortByColumn(INGRESOS_COL_N_RECIBO, Descending)` (src/listado/listado.cpp:221-ish, src/recog_prendas/recog_prendas.cpp:640), so "latest first" breaks at the digit rollover. The 11.2 fix for `MAX(n_recibo)` is the same trap. Same for INGRESOS_COL_CANTIDAD (8), SIZE (10), RECTIFIES_N_RECIBO (22). | Add a numeric set (n_recibo, cantidad, size) to `lessThan`; compare with `toDouble()` after `replace(',', '.')`. | S |
| Medium ✅ FIXED 20-10-2026 | bug | mysortfilterproxymodel.cpp:97-102 | `INGRESOS_COL_FECHA_ANULACION` (26) is a dd-MM-yyyy date shown with the other three (ingresoscolumns.cpp:10) but not in the date branch; sorting "Anulacion" is lexicographic (wrong across months/years). | Add it to the date branch. | S |
| Medium ✅ FIXED 20-10-2026 | bug | mysortfilterproxymodel.cpp:75 | Filter scans `qMin(columnCount, 10)` columns: Tam.(10), Serv.(11), Obs.(12) are visible in ingresos yet never searched. A `column` argument >= 10 can never match. The comment justifies the cap by verifactu_* columns past 14, so 13 (through Observaciones) would do. | Named constant (`INGRESOS_COL_OBSERVACIONES + 1`) or an explicit `setFilterColumns(QSet<int>)`. Fix docs either way. | S |
| Medium | bug | mysortfilterproxymodel.cpp:80-86; src/listado/listado.cpp:278-284 | Regex and normalized text are OR-ed. With "Distinguir mayusculas" ON (or a regex/wildcard meant to exclude), the normalized text still matches case/diacritic-insensitively, so the toggle never narrows results. An invalid regex silently falls back to substring. | Make the modes exclusive (Listado sets either regex or normalized), or document that the toggle only adds matches. Decide with the listado reviewer. | M |
| Medium ✅ FIXED 20-10-2026 | bug | src/listado/listado.cpp:21-24; src/tableview/filterwidget.cpp:16 | `FilterWidget` emits `filterChanged` on every `textChanged`; Listado connects BOTH `filterChanged` and `QLineEdit::textChanged` to `textFilterChanged`. Each keystroke = 2 runs x (setFilterRegularExpression invalidate + setNormalizedFilter invalidate + resizeColumnsToContents + resizeRowsToContents) = 4 filter passes + 2 full resizes. On 20k rows `removeDiacritics` allocates per cell (200k normalisations per pass). | Drop the `textChanged` connection (listado.cpp:23); add one `setFilter(regex, normalized, column)` that invalidates once; debounce 150 ms with a QTimer; resize columns only when the filter is cleared. | S |
| Medium ✅ FIXED 20-10-2026 | bug (unverified) | src/listado/listado.cpp:157-191 | Only non-ingresos tables are drained with `fetchMore` (:189). The proxy filters/sorts only fetched rows (256 for ingresos), so Listado ingresos text filter and header sorts may miss older tickets (RecogPrendas drains on purpose, recog_prendas.cpp:599-605). | Confirm at runtime; drain on first filter/sort with a wait cursor or filter in SQL. | M |
| Medium | bug | src/tableview/numberformatdelegate.cpp:26-38 | `NumberFormatDelegate` accepts any text: "abc" or "" is stored in gastos.importe and prendas prices (the Listado editValidator only checks the date, listado.cpp:166-179); display then shows "0,00" silently (`moneyText`), and "1.234,56" becomes "1.234.56". Accounting reads these columns. | `createEditor` with a validator (`^\d+([.,]\d{1,2})?$`) and refuse in `setModelData`. | S |
| Low ✅ FIXED 20-10-2026 | bug | mysortfilterproxymodel.cpp:106 | Numeric branch uses `toFloat()` and no comma handling: a legacy "12,5" sorts as 0. | `toDouble()` after replacing ',' by '.' (like `moneyText`). | S |
| Low | design | mysortfilterproxymodel.cpp:102 | `QDate::fromString` twice per comparison: about 600k parses to sort 20k rows. No trim (rowLocked trims, :30). Empty fecha_pago/recogida parses to an invalid QDate and sorts first - implicit. | Slice dd-MM-yyyy into a yyyyMMdd key (no QDate) or cache a key in `Qt::UserRole`; document "empty first". | S |
| Low | design | mysortfilterproxymodel.cpp:26-40, .h:43 | `flags()` re-parses the date on every call (Qt calls it many times); `closedQuarters` is a public mutable member that can go stale (Listado works around it with `rowLockedNow`, listado.cpp:396). | Private member + `setClosedQuarters()` that emits `dataChanged` over the fecha column. | S |
| Low | design | mysortfilterproxymodel.h:13-21,37 | Schema knowledge by string: public snake_case `table_name` (guidelines naming; set at listado.cpp:162, recog_prendas.cpp:636, tests) plus positional `GASTOS_IDX_*` / `LIST_PRENDAS_IDX_*` macros in a proxy header (INGRESOS_COL_* lives in ingresos_schema.h). | `enum class Kind` + `setKind()`, or a per-column map `setColumnKind(col, Date/Number/Text)` so callers own the schema; move macros to a schema header. | M |
| Low | design | tableview.h:16-18, tableview.cpp:8-9,16 | Public snake_case `action1`, `action2`, `context_menu` (meaningless names, used at listado.cpp:19-22); Spanish literals without `tr()` next to an English `tr("Context menu")`; `doubleClick` signal duplicates `QTableView::doubleClicked`; unused includes `QWidget/QMouseEvent` (h:5-6); ctor not `explicit`; empty destructor (cpp:20). | Rename `m_addRowAction/m_deleteRowAction/m_contextMenu` with accessors; drop `doubleClick` and connect `doubleClicked`; `tr()` the labels. | S |
| Low | dead-code | textcolordelegate.cpp:38-40, .h:21,25 | `paint()` only forwards to the base; `m_tableView` and the ctor `QTableView*` argument are never read. | Delete `paint`; ctor with parent only (4 call sites: 2 in listado.cpp:209-210ish, 2 in recog_prendas.cpp:660-661). | S |
| Low | dead-code | filterwidget.h:14-15, filterwidget.cpp:3-4,25,30,34,66 | `Q_PROPERTY caseSensitivity/patternSyntax` (READ-only) unused; `QIcon`/`QPixmap` includes unused; pattern menu strings lack `tr()` (only :19 has it); `patternSyntaxFromAction(checkedAction())` would crash on null (cannot happen while exclusive). | Remove properties/includes, add `tr()`, guard null. | S |
| Low ✅ FIXED 19-10-2026 | design | textcolordelegate.cpp:30-31, linkdelegate.cpp:7-8 | Hard-coded colours: green (0,180,0) on white is about 2.6:1 contrast; `Qt::blue` ignores dark mode. `TextColorDelegate` hard-wires ingresos sibling columns (works only for ingresos, undocumented). | Darker green; `palette.link()`; note "ingresos only" in the header. | S |
| Low | security | src/listado/listado.cpp:25-30 | Listado opens any `verifactu_url_qr` string with `QDesktopServices::openUrl` (local DB value, other schemes such as file: would run). Risk low, DB is trusted. | Open only `https://` URLs. | S |

## Documentation drift
1. docs/architecture.md:123 says the proxy "checks all columns against filter regex"; code scans only the first 10 source columns (mysortfilterproxymodel.cpp:75) and ORs regex with normalized text (:80-86). It also omits `table_name`, `rowLocked/flags`, `closedQuarters`, `editValidator/editRefused` (mysortfilterproxymodel.h:37-47), which docs/modules/listado.md:40 does describe.
2. docs/architecture.md:125 "formats numeric cells to 2 decimal places": now decimal comma, editor on the comma form, dot storage (`storedNumber`); `DecimalCommaDelegate` (numberformatdelegate.h:22-30) is missing from the table.
3. docs/architecture.md:122 "context menu (add/delete row)" does not name `action1/action2`, whose labels are hard-coded Spanish.
4. docs/modules/listado.md:58 lists the tableview types exposed by `listado` but omits `LinkDelegate`, `DecimalCommaDelegate`, `placeIngresosDateColumns`; docs/INDEX.md:122 and :133 omit `DecimalCommaDelegate`/`ingresoscolumns` in the CMake row.
5. docs/modules/recog_prendas.md:151 says `setSortingEnabled(true)` "is set on every search (in on_pb_search_clicked)"; it is set once in the constructor (recog_prendas.cpp:90). It also says `lessThan` special-cases "the three date columns": fecha_anulacion is a fourth (code bug above) and the n_recibo default sort is textual.
6. docs/modules/listado.md:38 "implemented via setNormalizedFilter called from textFilterChanged()" omits that the regex filter is set too (listado.cpp:278) and the two OR together; the case-sensitivity toggle has no visible effect.
7. docs/testing/unit_suites.md:83 coverage list needs the items below once written. tests/test_mysortfilterproxymodel.cpp:70-72 comment "literals to avoid the dependency" is stale (the proxy .cpp itself includes sql_lite.h/ingresos_schema.h, cpp:2-3; test_textcolordelegate already uses INGRESOS_COL_*).
8. mysortfilterproxymodel.h:9-11 is changelog text ("used to also carry INGRESOS_IDX_*"), not documentation; delete.

## Test gaps

Covered today: removeDiacritics (4 strings), accent-insensitive filter over all columns (no `column` arg), lessThan: ingresos date col 3, ingresos importe col 5, string default; classify (5 cases), placeIngresosDateColumns, storedNumber/setModelData, displayText/setEditorData.

### Unit (tests/test_mysortfilterproxymodel.cpp unless noted)
- `test_lessThan_ingresosNReciboNumeric`: TEXT "99999","100000","1000" ascending -> 1000, 99999, 100000 (fails today; drives the fix).
- `test_lessThan_fechaAnulacionChronological`: INGRESOS_COL_FECHA_ANULACION "31-12-2025" < "01-01-2026".
- `test_lessThan_emptyDatesSortFirst`: "" fecha_pago before dated rows ascending, last descending; ties keep source order.
- `test_lessThan_gastosColumns`: GASTOS_IDX_FECHA chronological, GASTOS_IDX_ID/IMPORTE numeric ("10" > "9"); `table_name="prendas"` LIMP/PLAN numeric; unknown `table_name` -> string order.
- `test_lessThan_importeWithCommaAndEmpty`: "12,5", "", "3.0" (pins behaviour).
- `test_filter_columnArgument`: `setNormalizedFilter("ana", 1)` matches only column 1, not column 0.
- `test_filter_columnScanLimit`: text in column 12 matches (after fix); text in column 14 (hash) never matches.
- `test_filter_regexAndNormalizedAreOr`: regex only, normalized only, both, invalid regex "[" -> pins behaviour; case-sensitivity option.
- `test_filter_nonStringAndNullCells`: int/double/null QVariant cells.
- `test_filter_performance_20kRows`: 20 000 rows x 14 columns, one filter pass under a generous limit; plus a sort of the date column.
- `test_removeDiacritics_edge`: "", precomposed vs decomposed "é", "Á" -> "A", non-Latin text and "ß" untouched.
- `test_rowLocked_gastos`: fecha "15-02-2026" with `closedQuarters={quarterKey(1,2026)}` -> true; open quarter, invalid/empty date, other `table_name` -> false; " 15-02-2026 " trimmed; takes a proxy row (check after a descending sort).
- `test_flags_dropsEditableOnlyOnLockedRows`: locked vs open row; invalid index; non-gastos unchanged.
- `test_editValidator_refusesAndEmitsEditRefused`: message -> `setData` false, source unchanged, `editRefused` spy count 1 with the text; empty message writes; non-EditRole bypasses; no validator writes.
- tests/test_textcolordelegate.cpp: `test_initStyleOption_usesSiblingEstadoColumns` (model with 28 columns, ANULADA in INGRESOS_COL_VERIFACTU_ESTADO turns "NO" green, check `option.palette.text()`), `test_classify_edgeCases` (empty, lowercase "si", "SIN COBRAR" -> Default), `test_numberFormat_invalidInput` (after validator fix), `test_decimalCommaDelegate_emptyAndInteger`, `test_linkDelegate_underlineOnlyWhenNotEmpty`.
- New tests/test_filterwidget.cpp (offscreen): defaults (RegularExpression, case-insensitive); `filterChanged` emitted exactly once per text change, once per case toggle, once per syntax pick (guards the Listado double connection).
- New tests/test_tableview.cpp (offscreen): `doubleClicked` forwards to `doubleClick`; the two menu actions exist and trigger.

### E2E (tests/test_e2e_app.cpp, existing Listado scenarios near 2799-2870)
- `test_listado_ingresosDefaultOrderNumericAndFilterSeesOldRows`: seed 300+ ingresos incl. n_recibo "99999" and "100000"; top row is 100000; a client existing only past row 256 is found by `filter_widget`.
- `test_listado_sortByFechaAnulacionAndFilterObservaciones`: click each of the four date headers and check chronological order; filter by a word only in Observaciones.
- `test_recogida_nameSearchAccentInsensitiveAndSort`: "garcia" finds "Garcia" and "García", then sort by Importe, Recepcion, Anulacion.
- `test_listado_gastosLockedRowsSurviveSortAndFilter`: the locked flag follows the row after sorting by Importe and after filtering.
- `test_listado_filterKeystrokeRunsOnce`: spy on `proxyModel` invalidations per typed character.

### UI smoke (tests/ui; block_b.py B7/B8 only check column adjacency and one accent search at block_b.py:153)
- B9: click the Nº, Importe and Anulacion headers in Listado ingresos and Recogida after a 99999/100000 seed; assert order from the UIA cells.
- B10: filter on a word in Observaciones; toggle "Distinguir mayusculas" from the options button and assert the row count changes (after the fix).
- Assert the red/green rendering (pixel sample of a "NO" cell and of an Anulado row); colours are never asserted today.
- docs/testing/smoke_test.md: manual rows "Nº descending shows the newest ticket first" and "filter finds an Observaciones word"; contrast of green/red on the shop monitor.

### Self-check (`laideal.exe --self-check`, tests/test_selfcheck.cpp)
- S: over the DB copy build the real `MySortFilterProxyModel` on ingresos (drained), sort by n_recibo descending and assert the first row equals `MAX(CAST(n_recibo AS INTEGER))`; count rows whose four date columns do not parse as dd-MM-yyyy or whose amounts contain a comma (what `lessThan`/`rowLocked` silently mishandle). Report as a "grid" line.

## Questions or cross-module notes
- listado reviewer: (1) confirm and remove the double connection `filterChanged` + `textChanged` (listado.cpp:21-24); (2) does the lazily fetched ingresos model (256 rows) restrict the filter/sort? (3) `TextColorDelegate(table_listado, this)` second argument becomes unused if `m_tableView` is deleted.
- recog_prendas reviewer: recog_prendas.md:151 drift on `setSortingEnabled`; `setNormalizedFilter(..., INGRESOS_COL_CLIENTE)` is fine (column 1 < 10).
- sql_lite reviewer: n_recibo TEXT is the root of the string sort; confirm `closedQuarterKeys` (sql_lite.cpp:1149) cannot insert key 0 (`quarterKey` of an invalid date) which would lock every row with an unparseable date.
- genlistado.h:32 has its own `table_name`; genlistado.cpp:150 duplicates `rowLocked`: share one helper `isDateInClosedQuarter(fechaText, closedKeys)`.
- Not run: no build, no tests, no shop DB opened. Items marked "unverified" need a runtime check.
