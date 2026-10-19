# Review: reporthtml (src/reporthtml/)

(Returned as text by the reviewer, whose Write was refused; saved here by the orchestrator.)

## Summary
- Small and correct on the happy paths: `formatEuro`, `tableOpen`, `documentClose` and the `writePdf` success check behave as documented. No Critical or High findings.
- Main weakness: user-entered text reaches the HTML unescaped. This affects `documentOpen` (business name, address and so on) and the Listado cells, so a "<" or a stray "&" corrupts the PDF. It is a rendering bug, not a security hole.
- Unit coverage is thin: 3 tests, none for `documentOpen` or `writePdf`, and none for negative zero, rounding at group boundaries or NaN.
- Doc drift is minor.

## Findings
| Sev | Cat | file:line | Finding | Proposal | Eff |
|---|---|---|---|---|---|
| Medium ✅ FIXED 20-10-2026 | bug | src/reporthtml/reporthtml.cpp:46-51 | `documentOpen` inserts the settings values (businessName, Address, City, verifactuNif, businessPhone) and title / subtitle raw. A name such as "A&B <x>" breaks the header. | Escape the settings values with `toHtmlEscaped()`. Document title / subtitle as HTML by contract. | S |
| Medium ✅ FIXED 20-10-2026 | bug | src/listado/genlistado.cpp:116, 211-214 | Listado cells (prenda, nFactura, servicio, empresa, fecha) are not escaped, while Contabilidad escapes the same kind of fields. A supplier named "R&D <S.L.>" breaks the table. | Escape every user cell, optionally through a `ReportHtml::cell()` helper. | S |
| Low | bug | reporthtml.cpp:121-129 | The result of `QFile::remove` is ignored. If a viewer locks the old PDF and QPrinter fails, the stale non-empty file passes the size check. | Fail when the old file exists and cannot be removed, or check its modification time. | S |
| Low ✅ FIXED 20-10-2026 | bug | reporthtml.cpp:131 | The result of `openUrl` is ignored. | Log a warning when it fails. | S |
| Low | bug | reporthtml.cpp:81-103 | -0.004 prints "-0,00 €". NaN / inf / huge values reach `qRound64` (undefined behaviour). Callers pass `toFloat()` and sum in float (genlistado.cpp:125,130,176,215-217). | Take the sign from the rounded cents, guard non-finite values, use double in the callers. | S |
| Low ✅ FIXED 20-10-2026 | design | reporthtml.cpp:104-135 | `writePdf` writes straight to the final path: a crash leaves a truncated file of size > 0 that passes the check. | Write to a temporary file, then rename. | M |
| Low ✅ FIXED 20-10-2026 | design | genlistado.cpp:95 | `print_table()` ignores the result of `writePdf`. | Propagate or log it. | S |
| Low | design | src/app/mainwindow.cpp:1539 | The declaración PDF is a bare `<html><body>`, without `documentOpen` / `documentClose`. | Confirm this is intended and document it. | S |
| Low | design | reporthtml.cpp:117-129 | No page numbers; Arial is only a preference and Qt substitutes a font silently. | Optional page footer, or document the behaviour. | M |
| Low | design | reporthtml.cpp | PDFs are Spanish-only, although the app has a language setting. | Document that this is by design. | S |
| Low | design | reporthtml.cpp:82-103 and the test | A literal "€" in the source is outside the SKILL.md exception list. | Write `€`, or extend the exception list. | S |
| Low | dead-code | reporthtml.h:36-45 | `lastReportHtml` exists only for the tests (intended). `<QFile>` and `<QPageSize>` arrive only through other includes. | Add the two includes explicitly. | S |
| Low | docs | reporthtml.h:5-9, 27-30 | The header names only two consumers and says zebra striping comes from `tableOpen(false)`, but the callers add the striping. The declaración is a third consumer. | Reword the header. | S |
| Low | unit-tests | tests/test_reporthtml.cpp:3-5 | The comment is stale on two counts: "€" is in fact written as a literal, and `documentOpen` is said to be untestable although `AppSettings::loadFrom` exists. | Fix the comment and add the tests below. | S |

## Documentation drift
- docs/architecture.md:30-31: the declaración (mainwindow.cpp:1539) is a third consumer.
- docs/modules/uikit.md:27: the `writePdf` claim holds only once the failed-remove case is fixed.
- docs/INDEX.md:117: there is no reporthtml module doc. Put the escaping contract and the test seams in architecture.md.
- docs/testing/unit_suites.md:100: update it when tests are added.

## Test gaps
**Unit tests, test_reporthtml:**
- `test_formatEuro_roundingAndEdges`, including -0.004 → "0,00 €" (fails today) and 999.995 → "1.000,00 €".
- `test_formatEuro_nonFinite`.
- `test_documentOpen_businessHeader`: escaping, identity line, subtitle and date.
- `test_documentOpen_isBalanced`.
- `test_writePdf_writesAValidPdf` (offscreen).
- `test_writePdf_replacesAnOlderFile`.
- `test_writePdf_unwritablePathReturnsFalse`.
- `test_writePdf_staleFileNotReportedAsSuccess` (after the fix).
- `test_tableOpen_defaults`.

**Unit tests, test_genlistado:** `test_gastosHtml_escapesSupplierAndService`, `test_prendasHtml_escapesDescription`.

**E2E:**
- Extend `test_genListadoGastos_pdfAndEmptyYear` with a supplier containing "&<".
- `test_reportHtml_businessHeaderInEveryReport`.
- `test_genListadoPrendas_printTableWritesPdf`.
- `test_declaracionResponsable_savesPdf` (needs a file-dialog seam).

**UI smoke:**
- Check page 1 of the reports with pypdf.
- The annual report with detail should have more than one page, with the header repeated.
- The listado PDF should be A4.
- Render PNGs for the manual look check (M4).

**Self-check:** a "PDF engine" check that writes a one-line PDF to a temp folder and verifies `%PDF-` and size > 0.

## Cross-module notes
- **Listado:** cell escaping, the ignored `writePdf` result, money handled as float.
- **sql_lite:** `displayMoney` (ungrouped, for windows) and `formatEuro` (grouped, for PDFs) are both intended. Document the split so nobody merges them.
- **App / Verifactu:** is the unstyled declaración PDF deliberate?
