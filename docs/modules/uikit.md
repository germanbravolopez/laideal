# UiKit (`src/uikit/`)

The shared look of the application's windows, in one place. Every dialog is built in code from these pieces (no `.ui` files), so the style is changed here once and every window follows.

## Source files

- `src/uikit/uikit.h/cpp` — static library `uikit` (links `Qt::Widgets`).

## The pieces

| Function / class | What it gives |
|---|---|
| `UiKit::setUpDialog(dialog, title, minimumWidth = 520)` | Title, minimum width, no "?" help button, no minimise button (a dialog is never sent behind its window). Modality stays with the caller (`setWindowModality`). |
| `UiKit::heading(text)` | Bold title, five points over the app font — for windows that list data (Listado). |
| `UiKit::introPanel(text)` | Framed explanation at the top: what the window does, in one or two sentences. Rich text allowed. |
| `UiKit::primaryButton(text, objectName)` | The action the window exists for: bold, default (Enter), at least 220 px wide. One per window, at the right of its row. |
| `UiKit::secondaryButton(text, objectName)` | Any other action; never takes Enter from the primary one. |
| `UiKit::dateEdit(date, objectName)` | Calendar popup, `dd-MM-yyyy`, wide enough for the whole date. |
| `UiKit::closeRow(dialog, text = "Cerrar")` | Right-aligned closing button (`btnClose`) that closes the dialog. |
| `UiKit::ResultPanel` | Framed `QLabel` (`lblResult`) where the window reports **every** outcome instead of message boxes; rich text, links open the file or URL. `showInfo(plain)` for neutral text, `setText(html)` with the phrases below. |
| `UiKit::sortedNames(list)` | Names for a list or combo box in Spanish alphabetical order: case-insensitive, accents in place (`QCollator`, es_ES) — a plain `ORDER BY` would put "Álvarez" after "Zurita". |
| `UiKit::okHtml` / `warnHtml` / `errorHtml` | Green success, amber "nothing done / attention", red error. |
| `UiKit::fileLinkHtml(file, label = "PDF")` | "PDF: name.pdf" linking to the file. |

Generated PDFs go through `ReportHtml::writePdf` (`src/reporthtml/`), the one PDF writer, which also opens the file (switched off in the tests with `ReportHtml::setOpenGeneratedReports(false)`).

## Layout of a window

Top to bottom: `introPanel`; the fields in `QGroupBox`es with a `QFormLayout` (`FieldsStayAtSizeHint` for short fields such as dates and amounts); the action row (secondary buttons on the left, stretch, the primary button on the right); the `ResultPanel`; `closeRow`. Fonts are the app font (or relative to it), never fixed point sizes or pixel stylesheets. Widgets carry camelCase object names so the e2e bench (`tests/test_e2e_app.cpp`) drives them by name.

Message boxes are kept only where they belong: confirming a destructive action (delete a row, void garments), and a notice when the window closes right after (Cobrar without AEAT, printing from an engine path with no window).

## Windows built with it

Generar / Revertir contabilidad, Formulario facturas, Añadir nuevas prendas, Generar listado de gastos, Imprimir (recibo / factura / factura completa), Listado windows and Nuevo cliente, Exportar registros AEAT, Cobrar, Envíos Verifactu pendientes, Actualización disponible, Acerca de Verifactu, Notas de la versión, Recogida de prendas (with its Verifactu and AEAT comparison windows), the main window (ticket entry). Rectificar factura, Anular factura and Anular prendas follow the same layout with their own code (older than the kit). No window uses a `.ui` form any more.
