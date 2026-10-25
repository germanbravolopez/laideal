# Review: uikit (src/uikit/uikit.h 87 lines, uikit.cpp 221 lines; tests/test_uikit.cpp)

Line refs are to the real files.

## Summary
- Small, clean module with ~16 consumers. No HTML injection found: every call site I read escapes user/AEAT data (`toHtmlEscaped`) before `okHtml/warnHtml/errorHtml`.
- One real behaviour bug against the docs: Enter on a focused `secondaryButton` (autoDefault=false) is not handled by the button, bubbles to the dialog filter and presses the PRIMARY button. Docs and test claim the opposite; the test only uses plain auto-default buttons.
- `fileLinkHtml` builds `href='...'` from `QUrl::toString()` (apostrophe / `&` not escaped): a path with an apostrophe breaks the link. Low risk, easy fix.
- Test coverage is thin: 5 tests, Enter rules and one AmountSpinBox case. html helpers, sortedNames, dateEdit, closeRow, ResultPanel, negatives and "1.234" are untested. e2e never types into AmountSpinBox (always `setValue`); UI smoke has no Enter-rule checks.
- Consistency: Rectify/Cancel dialogs still hand-build result label, Cerrar and date edit; SettingsDialog bypasses the kit; several QMessageBox uses outside the documented exceptions.

## Findings

| Severity | Category | file:line | Finding | Proposal | Effort |
|---|---|---|---|---|---|
| Medium ✅ FIXED 19-10-2026 | bug | uikit.cpp:64-90 (DialogEnterFilter), 137-142 (secondaryButton), 155-161 (closeRow) | `QPushButton::keyPressEvent` clicks on Enter only if `autoDefault()` or default. `secondaryButton` sets `setAutoDefault(false)`, so Enter on a focused secondary button (including closeRow's Cerrar) is ignored by the button, bubbles to the dialog, and the filter clicks the PRIMARY (Cobrar / Guardar / Reenviar). Contradicts uikit.h:30, uikit.md ("a focused button still presses itself") and test_uikit.cpp:96-103 (uses a plain QPushButton, not `secondaryButton`). A keyboard user tabbing to Cerrar and pressing Enter can fire the primary action (possibly an AEAT send). Based on Qt 6 source knowledge; not run. | In DialogEnterFilter, before looking for the primary: if `QApplication::focusWidget()` is a QAbstractButton, `click()` it and return true. Add a test with `secondaryButton`. | S |
| Medium ✅ FIXED 25-10-2026 | bug/design | uikit.cpp:64-90 | Enter bubbling from non-text widgets also presses the primary: QTableView/QListWidget (ignore Return when not editing), QCheckBox, non-editable QComboBox, QDateEdit. E.g. Enter on a table row in a dialog whose primary sends to AEAT or voids. | Decide the rule: react only when the focus source is QLineEdit / QAbstractSpinBox / editable combo; otherwise ignore. Document it. | S |
| Medium ✅ FIXED 25-10-2026 | bug | uikit.cpp:64-90 (`findChildren<QPushButton*>()`) | Recursive search includes primary buttons of child dialogs/windows and picks the first in child order. Nested windows parented to the dialog could be pressed from the parent. | Restrict to buttons with `button->window() == m_dialog`. | S |
| Low | security | uikit.cpp:177-181 | `fileLinkHtml`: `href='` + `QUrl::fromLocalFile(file).toString()` + `'`. PrettyDecoded does not percent-encode `'` or `&`, so a path like `C:/Users/O'Brien/...` ends the attribute early and breaks the link; `label` is not escaped. selfcheckdialog.cpp:300 passes a file name as `label` unescaped. | Use `QString::fromLatin1(QUrl::fromLocalFile(file).toEncoded()).toHtmlEscaped()` in a double-quoted href; escape `label` or document it as trusted HTML. | S |
| Low | security/design | uikit.cpp:173-175, 212 | `okHtml/warnHtml/errorHtml` take HTML and escape nothing; every caller must remember `toHtmlEscaped()`. All sites I sampled (mainwindow, recog_prendas, add_garment, imprimir, cancel/pay/void/updater, contabilidad, genlistado) do. ResultPanel has `setOpenExternalLinks(true)`, so one missed escape of AEAT text containing an `<a href>` would open a browser. `refuse(blocked)` at mainwindow.cpp:794-798 passes `numberingBlockedReason()` / `invoicingBlockedReason()` unescaped (internal strings today). | Add escaping variants (e.g. `okText(plain)`) and keep `okHtml` for trusted HTML, or add a grep rule to the guidelines audit (`(ok|warn|error)Html(` with `.arg(` lacking `toHtmlEscaped`). Document the contract in uikit.md. | S-M |
| Low | bug | uikit.cpp:103-112 | `heading()`: `pointSizeF()` is -1 for pixel-sized fonts, giving size 4. Same pattern copied at mainwindow.cpp:355,395, pay_dialog.cpp:76, recog_prendas.cpp:172, listado.cpp:102. | Guard `pointSizeF() > 0` or add `UiKit::scaledFont(base, delta)` and use it in those places. | S |
| Low | bug | uikit.cpp:94-101 | `setUpDialog` uses `setWindowFlag`, which hides an already-shown dialog; must be called before `show()` (all callers do). Undocumented. | Comment in uikit.h. | S |
| Low | bug/docs | uikit.cpp:183-199 | AmountSpinBox: `validate` replaces EVERY '.' by ','. Typed "1.234" with 2 decimals becomes "1,234" -> Invalid (third digit ignored, giving 1,23). A pasted "1.234,56" becomes "1,234,56" -> Invalid (paste silently rejected). `valueFromText("1.234")` gives 1.234 (rounded). Negatives work only if the caller's range allows (rectify -100000..100000, rectifyinvoicedialog.cpp:113); "-" alone is Intermediate. Behaviour is reasonable but undocumented and untested. | Document; add tests; optionally strip a Spanish thousands pattern before replacing. | S |
| Low | design | rectifyinvoicedialog.cpp:99-102; settingsdialog.cpp:225-227 | Hand-built QDateEdit (calendar popup, dd-MM-yyyy) instead of `UiKit::dateEdit` (no min width 140, no common look). | Use `UiKit::dateEdit(date, "deRectifyDate")`. | S |
| Low | design | rectifyinvoicedialog.cpp:134-142; cancelinvoicedialog.cpp:69-75; pendingsubmitsdialog.cpp:62-67 | Result label is a plain QLabel named lblResult (no frame, no min height, no TextBrowserInteraction/links) and Cerrar is hand-built instead of `ResultPanel` / `closeRow` (pending builds its own row with secondaryButton). These dialogs already use setUpDialog + onEnter. | Swap to `UiKit::ResultPanel` and `closeRow` keeping the object names (e2e finds them by name). | S |
| Low | design | settingsdialog.cpp:40-55,29,69,127,166,315-375 | SettingsDialog does not use `setUpDialog` (own QDialogButtonBox): no Enter rule (Enter in a field presses OK = save + close), "?" and minimise flags remain. 9 QMessageBox uses (127,315,323,333,339,375...), `setStyleSheet("color: gray;")` at 69,166, `setFixedWidth(90)` at 29. | Decide whether Enter=OK is wanted; at least call `setUpDialog` for flags; show test results in a ResultPanel; use palette colour instead of a stylesheet. | M |
| Low | design | pay_dialog.cpp:271; mainwindow.cpp:450,465,469,615; imprimir.cpp:437-439; main.cpp:85; sql_lite.cpp:431-574 (20 boxes) | QMessageBox outside the documented exceptions. pay_dialog.cpp:271 reports an AEAT submit failure while the dialog is open (should go to `m_lblStatus` with errorHtml). The connection-test dialog in mainwindow.cpp:450-469 could use its panel. imprimir.cpp:437 is the documented "no window" case. sql_lite modal boxes with nullptr parent in a data layer are known (unit_suites.md:36). | pay_dialog and test dialog -> panels (S each); sql_lite -> return error string and let the caller show it (L, file in tracker). | S/L |
| Low | design | filterwidget.cpp:45 | Only `setStyleSheet` pixel-style hit outside settingsdialog ("border: none"). Fixed point sizes: none (all relative to the app font, as the rule allows). reporthtml.cpp px sizes are print HTML, fine. | None. | - |
| Low | dead-code | uikit.h:68-73 | No dead function in the kit: heading (listado.cpp:67), closeRow (8 callers), sortedNames (facturas.cpp:122,128 only), fileLinkHtml (5 callers). sortedNames is used for only two combos: other name lists (clients, garment types, providers in listado / insertnewitem / mainwindow) may still use SQL ORDER BY and show "Alvarez" after "Zurita" despite the doc promise. | Grep other combos/lists filled from the DB and route through sortedNames, or state in the doc where it applies. | S |
| Low | unit-tests | tests/test_uikit.cpp | See test gaps. | Add ~20 tests. | M |

## Documentation drift

| Doc (file:line) | Says | Code |
|---|---|---|
| docs/modules/uikit.md (setUpDialog row), uikit.h:30 | "a focused button still presses itself" | False for `secondaryButton` / `closeRow` buttons (autoDefault=false): Enter goes to the primary (uikit.cpp:137-142 + 64-90). |
| uikit.md last paragraph | Rectificar factura, Anular factura and Anular prendas "follow the same layout with their own code (older than the kit)" | They already call `setUpDialog` and `onEnter`; only result label, Cerrar and date edit are hand-built in Rectificar / Anular factura (rectifyinvoicedialog.cpp:99,134-142; cancelinvoicedialog.cpp:69-75). Reword to the specific leftovers. |
| uikit.md "Windows built with it" / "No window uses a .ui form any more" | Lists windows | Omits Configuración (settingsdialog.cpp has no setUpDialog) and Listado (a QMainWindow using heading / introPanel / ResultPanel only). SKILL.md:67 still speaks of "the remaining .ui forms". |
| SKILL.md:65 "never QLineEdit::returnPressed" | absolute | recog_prendas.cpp:511 `on_le_search_returnPressed` is fine because Recogida is a QMainWindow, but the exception is not stated. |
| uikit.md `okHtml` row | "Green success, amber..." | No mention that the text is NOT escaped; callers must escape. Add the contract. |
| uikit.md `onEnter` row | "Runs once per press ... only while input is acceptable" | Matches uikit.cpp:36-45; but Enter is consumed (return true, uikit.cpp:42) even when input is not acceptable: say so. |
| docs/testing/unit_suites.md:28 | test_uikit covers AmountSpinBox and Enter rules | Accurate but incomplete vs the module (no helpers, no ResultPanel, no sortedNames). |
| docs/INDEX.md:136 ✅ FIXED 23-10-2026 | Lists `test_versioncompare` | No such file in tests/ (other module; supervisor to check). |

## Test gaps

### Unit (tests/test_uikit.cpp, 5 tests today)
Covered: AmountSpinBox comma display + "." and "," typing (one value); Enter in field -> primary (Return and keypad Enter); no primary -> nothing; focused plain button; auto-repeat; disabled primary; two primaries; onEnter once / auto-repeat / validator.

Untested, proposed tests:
- `test_enterOnFocusedSecondaryButton_pressesItself`: `secondaryButton("Buscar")` focused, press Return -> its clicks == 1, primary 0 (fails today).
- `test_closeRow_closesDialog`: button `btnClose`, default text "Cerrar", custom text, click -> dialog hidden.
- `test_setUpDialog_flagsAndMinWidth`: title set, no ContextHelp and no Minimize hint, `minimumWidth() == 520` and a custom value.
- `test_enterIgnoresChildDialogPrimary`: a primary inside a child dialog is not pressed (after the window() restriction).
- `test_enterFromTableOrCheckBox_policy`: documents the chosen rule for non-text widgets.
- `test_primaryButton_properties`: bold, `isDefault()`, `minimumWidth() >= 220`, objectName; `secondaryButton`: `!autoDefault()`.
- `test_introPanel_wordWrapFramedRich`: objectName lblIntro, wordWrap, StyledPanel, HTML text kept.
- `test_heading_boldLargerThanApp`: pointSizeF == app + 5, bold; pixel-font case stays > 0 after the fix.
- `test_dateEdit`: calendar popup, format dd-MM-yyyy, `text() == "05-03-2026"` for 2026-03-05, min width >= 140, objectName.
- `test_sortedNames_spanishOrder`: {"Zurita","Álvarez","alvarez","Ñandú","Nuñez","Zapata"} -> Álvarez beside alvarez before Zapata, Ñ after N, case-insensitive, empty list and duplicates safe.
- `test_okWarnErrorHtml_wrapAndColor`: contain `color:green`, `#b26a00`, `color:red`, inner text intact; plus escaping variants if added.
- `test_fileLinkHtml_escapesAndLinks`: file `C:/a b/O'Brien & Co.pdf` -> one anchor (parse with QTextDocument, check `anchorAt` / href), displayed `O'Brien &amp; Co.pdf`, default label "PDF", href starts `file:///`.
- `test_resultPanel_defaults`: objectName lblResult, RichText, openExternalLinks, min height >= 64; `showInfo("a<b>&\nc")` -> text `a&lt;b&gt;&amp;<br>c`; initialText constructor.
- `test_amount_negativeAndRange`: range -100..100, keyClicks "-5,5" -> -5.5; "-" -> Intermediate; with minimum 0, "-5" -> Invalid.
- `test_amount_thousandsDot`: validate("1.234") Invalid at 2 decimals; "1.23" Acceptable; "1,2,3" Invalid; empty Intermediate; pasted "1.234,56" outcome documented.
- `test_amount_localeIndependent`: with `QLocale::setDefault(English)`, text "12,50" and "12345,67" (no group separator).
- `test_amount_suffixEuro`: suffix " €", typed "7.25" -> 7.25, text "7,25 €".
- `test_onEnter_twoFieldsIndependent` and a null-action guard.

### E2E (tests/test_e2e_app.cpp)
- Rectificar: type "7.25" / "7,25" with `QTest::keyClicks` into `sbAmount` (all uses at lines 463,507,537,674,698,742,762,793,902 use `setValue`, bypassing the validator) and check the sent rectificative amount is 7.25; negative delta typed in I mode.
- Enter in each dialog with a primary: Formulario facturas (saves one row), Generar contabilidad, Exportar AEAT, Cobrar (charges once), Envíos pendientes (Enter on the table), Configuración.
- Tab to Cerrar / a secondary button + Enter: the primary must not fire.
- HTML escaping: save a ticket with client `<b>X</b> & "Y"` and a garment name with `<i>`; assert `lblResult->text()` contains the escaped form in Recogida, Imprimir and Añadir prendas.
- QMessageBox allow-list in the modal closer (docs/testing/e2e.md:33): fail on any title outside the documented exceptions so new message boxes are noticed.
- `fileLinkHtml`: Contabilidad into a reports folder with an apostrophe; the panel's link target equals the file.

### UI smoke (tests/ui)
- `type_into(..., enter=True)` is only used in Recogida / Imprimir (actions.py:58,232): add real-keyboard Enter checks for Facturas (saves once), Anular prendas, Configuración.
- Type "7.25" and "7,25" in the Rectificar amount with pywinauto and read the panel.
- Keyboard walk: Tab to Cerrar then Enter closes without triggering the primary (native Windows style differs from offscreen).
- Visual check at 125% / 150% DPI: introPanel, primaryButton (220 px) and dateEdit (140 px) not truncated.

### Self-check (--self-check, selfcheckdialog)
- Add an "Interfaz" check: instantiate each dialog class hidden and assert lblResult / btnClose / title / min width; verify the report link from `fileLinkHtml` round-trips (selfcheckdialog.cpp:300 passes the file name as an unescaped label).
- Decimal comma: AmountSpinBox text for a sample amount under the shop's Windows locale.

## Proposals summary (effort)
1. Fix Enter on focused secondary buttons + test (S), priority before 12.0 because the primary may send to AEAT.
2. Define and document the Enter policy for non-text widgets; restrict to `window() == dialog` (S).
3. Harden `fileLinkHtml` href / label escaping (S).
4. Escaping variants or an audit grep for the html helpers (S-M).
5. Replace hand-built date edits, result labels and Cerrar in Rectificar, Anular factura, Settings (S each).
6. ~20 unit tests; e2e typed-amount, Enter-in-every-dialog, escaping and QMessageBox allow-list (M).
7. pay_dialog.cpp:271 and the connection-test boxes to panels (S); sql_lite modal boxes (L, tracker).
8. Font helper for `pointSizeF() <= 0` (S).
9. Docs: fix the "focused button presses itself" claim, the "own code" paragraph, the windows list, the escaping contract (S).

## Questions / cross-module notes
- For app / recog_prendas / verifactu reviewers: `numberingBlockedReason()` and `invoicingBlockedReason()` reach `errorHtml` unescaped (mainwindow.cpp:794-798); confirm they never contain AEAT or user text.
- pendingsubmitsdialog: is Enter on its table meant to trigger its primary (depends on proposal 2)?
- Does Configuración keep Enter = OK? Needed before routing it through setUpDialog.
- docs/INDEX.md:136 lists `test_versioncompare`, absent from tests/ (docs reviewer).
- Nothing was built or run; the secondary-button Enter finding rests on Qt 6 `QPushButton::keyPressEvent` semantics and should be confirmed by the proposed test.
