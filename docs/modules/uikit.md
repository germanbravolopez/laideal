# UiKit (`src/uikit/`)

The shared look of the application's windows, in one place. Every dialog is built in code from these pieces (no `.ui` files), so the style is changed here once and every window follows.

## Source files

- `src/uikit/uikit.h/cpp` — static library `uikit` (links `Qt::Widgets`).

## The pieces

| Function / class | What it gives |
|---|---|
| `UiKit::setUpDialog(dialog, title, minimumWidth = 520)` | Title, minimum width, no "?" help button, no minimise button (a dialog is never sent behind its window). Call it before the dialog is shown (changing a window flag hides a shown window). Modality stays with the caller (`setWindowModality`). Also the dialog's **Enter rule**: Enter in a text field (line edit, spin or date box, editable combo) presses the enabled primary button of the dialog itself (not of a child window); Enter on a table, list, check box or fixed combo does nothing; or nothing when the dialog has none (Qt would press whichever button it made the default - a Buscar resetting the form, a Reintentar sending to AEAT); a focused button still presses itself; a held key never repeats. |
| `UiKit::heading(text)` | Bold title, five points over the app font — for windows that list data (Listado). |
| `UiKit::introPanel(text)` | Framed explanation at the top: what the window does, in one or two sentences. Rich text allowed. |
| `UiKit::primaryButton(text, objectName)` | The action the window exists for: bold, default (Enter), at least 220 px wide. One per window, at the right of its row. |
| `UiKit::secondaryButton(text, objectName)` | Any other action; never takes Enter from the primary one. A focused secondary button (Cerrar included) pressed with Enter presses itself, never the primary one (`setUpDialog`'s Enter filter clicks the dialog's focused button first). |
| `UiKit::onEnter(lineEdit, action)` | Enter in a field runs `action` and stops there (an event filter consumes the key). Use it instead of `QLineEdit::returnPressed`, which lets the key go on to the dialog's default button: that button then ran too (Imprimir printed twice; a ticket search also pressed Añadir prenda / Anular seleccionadas). Runs once per press (auto-repeat ignored) and only while the field's input is acceptable; the key is consumed even when the input is not acceptable (nothing else runs). Used by Imprimir, Añadir prendas, Anular prendas, Anular factura and Rectificar factura. |
| `UiKit::dateEdit(date, objectName)` | Calendar popup, `dd-MM-yyyy`, wide enough for the whole date. |
| `UiKit::closeRow(dialog, text = "Cerrar")` | Right-aligned closing button (`btnClose`) that closes the dialog. |
| `UiKit::AmountSpinBox` | `QDoubleSpinBox` that shows the decimal comma whatever the language and takes "," or "." as the decimal key (the numeric keypad sends either). With a comma already typed or pasted, dots are thousands separators and dropped ("1.234,56" is 1234.56); a lone "1.234" is a third decimal and is refused at two decimals. Negatives need the caller's range to allow them (Rectificar: -100000..100000). Used for the Rectificar factura amount. |
| `UiKit::ResultPanel` | Framed `QLabel` (`lblResult`) where the window reports **every** outcome instead of message boxes; rich text, links open the file or URL. `showInfo(plain)` for neutral text, `setText(html)` with the phrases below. |
| `UiKit::sortedNames(list)` | Names for a list or combo box in Spanish alphabetical order: case-insensitive, accents in place (`QCollator`, es_ES) — a plain `ORDER BY` would put "Álvarez" after "Zurita". Used today by the two combos of Formulario facturas; other name lists (clientes, prendas, proveedores) still come ordered by SQL. |
| `UiKit::okHtml` / `warnHtml` / `errorHtml` | Green success, amber "nothing done / attention", red error. **They take HTML and escape nothing**: the caller runs any user, client or AEAT text through `toHtmlEscaped()` (the `ResultPanel` opens links, so an unescaped `<a href>` would open a browser). |
| `UiKit::fileLinkHtml(file, label = "PDF")` | "PDF: name.pdf" linking to the file; the target is percent-encoded and HTML-escaped (an apostrophe or `&` in the path keeps the link whole) and the label is plain text, escaped here. |

Generated PDFs go through `ReportHtml::writePdf` (`src/reporthtml/`), the one PDF writer: it removes an earlier file of the same name first and reports success only for a non-empty file actually written, and it also opens the file (switched off in the tests with `ReportHtml::setOpenGeneratedReports(false)`).

## Layout of a window

Top to bottom: `introPanel`; the fields in `QGroupBox`es with a `QFormLayout` (`FieldsStayAtSizeHint` for short fields such as dates and amounts); the action row (secondary buttons on the left, stretch, the primary button on the right); the `ResultPanel`; `closeRow`. Fonts are the app font (or relative to it), never fixed point sizes or pixel stylesheets. Widgets carry camelCase object names so the e2e bench (`tests/test_e2e_app.cpp`) drives them by name.

Message boxes are kept only where they belong: confirming a destructive action (delete a row, void garments), and a notice when the window closes right after (Cobrar without AEAT, printing from an engine path with no window).

## Windows built with it

Generar / Revertir contabilidad, Formulario facturas, Añadir nuevas prendas, Generar listado de gastos, Imprimir (recibo / factura / factura completa), Listado windows and Nuevo cliente, Exportar registros AEAT, Cobrar, Envíos Verifactu pendientes, Actualización disponible, Acerca de Verifactu, Notas de la versión, Recogida de prendas (with its Verifactu and AEAT comparison windows), the main window (ticket entry). Rectificar factura, Anular factura and Anular prendas also call `setUpDialog` and `onEnter` (Enter never sends an invoice); only some pieces are still hand-built there: the result label and Cerrar button of Rectificar / Anular factura and the date edit of Rectificar (instead of `ResultPanel`, `closeRow` and `dateEdit`). Configuración (`SettingsDialog`) does not use the kit at all (own button box, message boxes, stylesheet), and Listado is a `QMainWindow` that only borrows `heading`, `introPanel`, the buttons and `ResultPanel`. No window listed above uses a `.ui` form.

Colours: `UiKit::COLOR_OK` #1B7F3B, `COLOR_ERROR` #C62828, `COLOR_WARN` #8A5300 and `COLOR_LINK` #0B57D0, all at least 4.5:1 on white. `okHtml` / `warnHtml` / `errorHtml`, the Recogida / Listado text colours (`TextColorDelegate`) and links (`LinkDelegate`) use them.
