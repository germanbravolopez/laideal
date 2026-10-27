# Listado (`src/listado/`)

Generic list-view window. Set `tableName` at runtime to display any DB table. Used by `MainWindow` for `ingresos`, `gastos`, `prendas`, `clientes`, `proveedores`, and `servicios`.

A `QMainWindow` built in code with the shared [UiKit](uikit.md) style (no hand-copied form code, no fixed 10 / 26 / 8 pt fonts): a heading (`lbl_title`, set by MainWindow), an explanation panel worded per table, an action row - **Añadir fila**, **Eliminar fila**, **Generar PDF** (each shown only where the table supports it), the search box and **Actualizar** - the table (one point under the app font, as it is wide), and a result panel where every message appears: a new gasto points to Formulario facturas, a row of a closed quarter cannot be edited, nothing selected to delete, a row added / deleted, a PDF written. The menus and their shortcuts remain and trigger the same actions; only deleting a row still asks for confirmation (it names the row by its first two visible cells).

**Menus and shortcuts**: `Archivo > Actualizar` (F5; Ctrl+A stays Select All in the table) and `Herramientas > Añadir fila` (Ctrl+N), `Eliminar fila` (Ctrl+D), `Generar listado en PDF` (Ctrl+P). The wording is the pre-12.0 one; the main window's Tickets / Facturas / Contabilidad regrouping was not applied here. The table's right-click menu offers Añadir / Eliminar fila enabled only where the table supports them (`configureForTable`).

## Source files

| File | Purpose |
|------|---------|
| `src/listado/listado.h/cpp` | Main list viewer |
| `src/listado/insertnewitem.h/cpp` | Dialog for inserting a new row into `clientes` |
| `src/listado/genlistado.h/cpp` | Dialog for generating garment/expense PDF reports |

## Key interface

```cpp
Listado *ui_listado = new Listado(db, this);  // db injected via constructor
ui_listado->tableName = "prendas";  // set before calling populateTable()
ui_listado->populateTable();
ui_listado->show();
```

## Signals

```cpp
signals:
    void populateClientes();  // emitted when the clientes window closes
    void populatePrendas();   // emitted when the prendas window closes
```

Both fire in `closeEvent` only; proveedores and servicios emit nothing.

`MainWindow` connects to these to refresh its client and garment comboboxes after edits.

## Features

- Add / delete rows via the buttons, the menu actions or the table's context menu - only on the tables that support them: **prendas, proveedores, servicios** add a blank row (`insertNewItemToTable`, a refused insert is reported) and clientes opens Nuevo cliente; **gastos** deletes but does not add (new ones come from Formulario facturas); **ingresos** neither (the refusal messages point to Anular prendas / Anular factura). Delete reports a refused `removeRow` instead of "Fila eliminada."
- **Ingresos view**: `NumberFormatDelegate` on importe, `DecimalCommaDelegate` on the size, `TextColorDelegate` on pagado / estado, `LinkDelegate` on the QR column (a click opens the address only when it is `https://`, `Listado::isOpenableQrUrl`), the hash / csv / xml / cancel-xml / seq / invoice-id / `edit_lock` columns hidden and the four date columns placed by `placeIngresosDateColumns` (see `tableview`). The gastos `edit_lock` column is hidden too.
- Gastos edits are checked in the proxy's `editValidator`: importe and IVA must be numbers (comma or dot), the `id` is never editable, a padded fecha is stored trimmed (so the year filter and the sort read it), and a write SQLite refuses is reported in the result panel.
- Text filter via `FilterWidget` (backed by `MySortFilterProxyModel`; one invalidation per keystroke through `setSearchText`; ingresos searches up to Observaciones, never the Verifactu metadata columns). Sorting is numeric for `n_recibo` (past 99999), amounts, quantities and sizes (decimal comma read), and by date for the four ingresos dates
- The prendas PDF reports a failed write in red (`print_table` returns the `writePdf` result); report cells are HTML-escaped
- **One search, no modes**: the box has no options menu. Accents and case are ignored ("alvarez" finds Alvarez, Álvarez and álvarez) and, with several words, a row is shown when it contains every word, in any searched column and order ("ana mora" finds "Mora, Ana"). Characters such as `^` or `*` are plain text. `textFilterChanged()` calls `MySortFilterProxyModel::setSearchText` once per change.
- PDF export via `GenListado` dialog (`actionGenerar_pdf_con_el_listado`)
- Inline cell editing via double-click for editable tables; a `gastos` row whose `fecha` is in a quarter closed by Contabilidad reports "Edición bloqueada" in the result panel, is read-only (`MySortFilterProxyModel::flags` drops `ItemIsEditable`; `rowLocked` against `closedQuarters`, the `closedQuarterKeys` Listado loads) and cannot be deleted ("Fila bloqueada") - revert the Contabilidad to change it. Delete, double click and every edit re-read the closed quarters first (`Listado::rowLockedNow`), so a quarter closed while the listing is open is honoured. An open row's `fecha` is checked before it is written (the proxy's `editValidator`, set by Listado): a date in a closed quarter (`quarterIsClosed`, the Formulario facturas rule) or not written as dd-mm-aaaa is refused in the result panel and the cell keeps its value. **`ingresos` is read-only in this view**, at the model level too: the proxy drops `ItemIsEditable` and refuses `setData` for ingresos, besides `setEditTriggers(NoEditTriggers)`, applied so the operator cannot bypass AEAT / the chained Huella / the accounting lock by editing cells directly. All `ingresos` changes must go through RecogPrendas / CancelInvoiceDialog / RectifyInvoiceDialog (Verifactu Req. 1, Art. 8.1 RD 1007/2023).
- Auto-resize window to table content, capped at the current screen's available width (`screen()->availableGeometry().width()`) so the window never extends off-screen

## Data loading strategy (`populateTable`)

`QSqlTableModel` fetches rows lazily in batches of 256. Two strategies are used:

| Table | Strategy | Reason |
|-------|----------|--------|
| `ingresos` | SQL-level `ORDER BY n_recibo DESC` — first batch is the most recent 256 rows; older rows load lazily as the user scrolls. A search or a header click drains the model first (`Listado::loadEveryRow`), so they see every row | `ingresos` can have thousands of rows; loading all upfront is slow |
| All others | `model->fetchMore()` loop until `canFetchMore()` is false — all rows loaded immediately | Smaller tables where full load is fast and filters need all data |

After the initial load `verticalHeader()->setDefaultSectionSize(rowHeight(0))` locks the compact row height so lazily-fetched rows match the initially-sized rows and don't expand unexpectedly on scroll.

## Dependencies (CMake)

`listado` links `tableview` PUBLIC (exposing `FilterWidget`, `MySortFilterProxyModel`, `TableView`, `NumberFormatDelegate`, `DecimalCommaDelegate`, `TextColorDelegate`, `LinkDelegate` and `ingresoscolumns` to its consumers) and `sql_lite`, `appsettings`, `reporthtml`, `uikit` PRIVATE (`src/listado/CMakeLists.txt`).

The price / amount columns (prendas `precio_limpieza` / `precio_plancha`, gastos `importe`) use `NumberFormatDelegate`, which shows two decimals with the comma whatever the language (an empty cell stays empty), opens the editor on the comma form and stores an edit with a dot (`storedNumber`: "2,5" → "2.5"). Since 12.0 that replaces the silent comma clean-up MainWindow ran after every Listado edit (`cleanDatabase(false)`).

## Sub-classes

### InsertNewItem
`QDialog` **Nuevo cliente** (UiKit style, window-modal) opened by Añadir fila in Listado de clientes: Nombre (required), Teléfono fijo, Móvil, Dirección and **Guardar cliente**; a missing name or a failed insert (`insertNewItemToTable` now returns whether it wrote) is shown in its result panel, and it closes itself once the client is saved. Cancelling adds nothing.

### GenListado
`QDialog` for generating garment or expense PDF reports, built in code with the shared [UiKit](uikit.md) style (no `.ui`). The expense listing (Listado de gastos → Generar PDF) shows a **Listado** group (Año + Todos los años, Gastos: all / only closed quarters, Agrupar por fecha / proveedor), **Generar listado PDF**, the result panel and Cerrar; the dialog stays open, the PDF is always regenerated (an earlier one of the same day is overwritten) and linked in the result panel, and a configuration without rows is reported there instead of a message box. The garment listing (`print_table()`) writes its PDF without showing the dialog and Listado reports the write result with a link. Neither listing reorders the table the user sees: rows are ordered on a copy (`rowsSortedBy`: by supplier then date for "Agrupar por proveedor"). Supplier groups end in an IMPORTE TOTAL row sized to the table (7 columns, or 8 with **Cerrado por contabilidad** - Si / No - which the "Incluir todos los gastos" type adds). Amounts are summed in whole cents (`splitGasto`) and the year filter ignores spaces around the fecha (`rowYear`). Both render through `ReportHtml::writePdf`. Output paths are read from `AppSettings::listadosPrendasPath()` (→ `<reports.root>/Listados/Prendas`) and `AppSettings::listadosGastosPath()` (→ `<reports.root>/Listados/Gastos`); both call `QDir::mkpath()` on demand.
