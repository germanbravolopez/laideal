# UiKit (`src/uikit/`)

The shared look of the application's windows, in one place. Every dialog is built in code from these pieces (no `.ui` files), so a style change is made here once. Source: `src/uikit/uikit.h/cpp` (static lib `uikit`, links `Qt::Widgets`). Tests: `tests/test_uikit.cpp`, see [unit suites](../testing/unit_suites.md).

## The pieces

| Function / class | What it gives |
|---|---|
| `setUpDialog(dialog, title, minimumWidth = 520)` | Title, minimum width, no "?" and no minimise button, plus the dialog's **Enter rule** (below). Call before the dialog is shown. Modality stays with the caller |
| `heading(text)` | Bold title, five points over the app font, for windows that list data |
| `emphasize(widget, extraPoints)` | Bold and enlarged over the app font (totals, ticket number) |
| `lockDate(edit, toolTip)` | Display-only date: read-only, no spin buttons |
| `limitDateRange(edit, earliest, latest)` | Maximum `latest`; minimum the latest of the valid `earliest` dates not after `latest`; no such date leaves the minimum as it is |
| `introPanel(text)` | Framed explanation at the top, one or two sentences; rich text allowed |
| `primaryButton(text, objectName)` | The action the window exists for: bold, default, at least 220 px; one per window, right of its row |
| `secondaryButton(text, objectName)` | Any other action; never takes Enter from the primary one |
| `onEnter(lineEdit, action)` | Enter runs `action` and stops there (the key is consumed), once per press, only while the input is acceptable. Use it instead of `returnPressed`, which lets the key reach the default button (Imprimir printed twice) |
| `dateEdit(date, objectName)` | Calendar popup, `dd-MM-yyyy`, wide enough for the date |
| `closeRow(dialog, text = "Cerrar")` | Right-aligned closing button (`btnClose`) |
| `AmountSpinBox` | `QDoubleSpinBox` showing the decimal comma whatever the language; takes "," or "." as the decimal key. With a comma typed or pasted, dots are thousands separators ("1.234,56" is 1234.56); a lone "1.234" is refused at two decimals. Negatives need the caller's range |
| `ResultPanel` | Framed `QLabel` (`lblResult`) where the window reports every outcome instead of message boxes; rich text, links open the file or URL. `showInfo(plain)`, `setText(html)` |
| `sortedNames(list)` | Spanish alphabetical order (case-insensitive, accents in place, `QCollator` es_ES), for combo and list contents |
| `okHtml` / `warnHtml` / `errorHtml` | Green success, amber attention, red error. **They take HTML and escape nothing**: the caller runs user, client or AEAT text through `toHtmlEscaped()` (the panel opens links) |
| `fileLinkHtml(file, label = "PDF")` | Link to a file; the target is percent-encoded and HTML-escaped, the label escaped |

Colours: `COLOR_OK` #1B7F3B, `COLOR_ERROR` #C62828, `COLOR_WARN` #8A5300, `COLOR_LINK` #0B57D0, all at least 4.5:1 on white. The phrases, `TextColorDelegate` and `LinkDelegate` use them.

### Enter rule (`setUpDialog`)

- Enter in a text field (line edit, spin or date box, editable combo) presses the enabled primary button of the dialog itself, not one of a child window; with no primary button it does nothing.
- Enter on a focused `QPushButton` presses that button (Cerrar never fires the primary action). Only a push button: a check box or radio button does nothing.
- Enter on a table, list or fixed combo does nothing. A held key never repeats.

## Layout of a window

Top to bottom: `introPanel`; fields in `QGroupBox`es with a `QFormLayout` (`FieldsStayAtSizeHint` for short fields); the action row (secondary buttons left, stretch, primary right); `ResultPanel`; `closeRow`. Fonts are the app font or relative to it, never fixed sizes or pixel stylesheets. Widgets carry camelCase object names so the e2e bench (`tests/test_e2e_app.cpp`) drives them by name.

Message boxes remain only for confirming a destructive action (delete a row, void garments) and for a notice when the window closes right after (Cobrar without AEAT, an engine print with no window).

Windows built with it: every dialog (all call `setUpDialog`, including Configuración), and the main window's ticket entry. `Listado` is a `QMainWindow` that borrows `heading`, `introPanel`, the buttons and `ResultPanel`.

## PDFs

Generated PDFs go through `ReportHtml::writePdf` (`src/reporthtml/`), the one writer: it removes an earlier file of the same name first, reports success only for a non-empty file actually written, and opens it (off in tests with `ReportHtml::setOpenGeneratedReports(false)`).
