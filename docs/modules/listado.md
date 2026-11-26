# Listado (`src/listado/`)

Generic list window: set `tableName` and it shows and edits one table (`ingresos`, `gastos`, `prendas`, `clientes`, `proveedores`, `servicios`). Opened by `MainWindow`. The table widgets (`FilterWidget`, `MySortFilterProxyModel`, delegates) live in `src/tableview/`. Tests: `test_genlistado.cpp`, `test_mysortfilterproxymodel.cpp`, `test_textcolordelegate.cpp`, see [unit suites](../testing/unit_suites.md). Columns: [architecture](../architecture.md).

| File | Purpose |
|------|---------|
| `listado.h/cpp` | The list window |
| `insertnewitem.h/cpp` | Nuevo cliente dialog |
| `genlistado.h/cpp` | Gastos PDF dialog and the prendas PDF |

```cpp
Listado *ui = new Listado(db, this);
ui->tableName = "prendas";   // before populateTable()
ui->populateTable();
ui->show();
```

## Window

A `QMainWindow` built in code with [UiKit](uikit.md): heading (`lbl_title`, set by MainWindow), explanation per table, action row (**Añadir fila**, **Eliminar fila**, **Generar PDF**, each only where the table supports it, search box, **Actualizar**), the table, and a result panel where every message appears. Only deleting a row asks for confirmation (it names the row by its first two visible cells). The window resizes to the table, capped at the screen's available width.

Menus: `Archivo > Actualizar` (F5, reloads the data via `populateTable`; Ctrl+A stays Select All), `Herramientas > Añadir fila` (Ctrl+N), `Eliminar fila` (Ctrl+D), `Generar listado en PDF` (Ctrl+P). The table's context menu offers Añadir / Eliminar where supported (`configureForTable`).

Signals `populateClientes` / `populatePrendas` fire in `closeEvent` only, so MainWindow refreshes its client and garment combos; the other tables emit nothing.

## Rules per table

| Table | Add | Delete | Edit |
|-------|-----|--------|------|
| prendas, proveedores, servicios | blank row (`insertNewItemToTable`; a refused insert is reported) | yes | inline; prendas prices are an amount or empty |
| clientes | opens Nuevo cliente | yes | inline |
| gastos | no (the message points to Contabilidad → Añadir factura de gastos) | yes, unless locked | inline, checked by the proxy's `editValidator` |
| ingresos | no | no | none |

- **`ingresos` is read-only**, at model level too: the proxy drops `ItemIsEditable` and refuses `setData`, besides `NoEditTriggers`. Changes go through RecogPrendas / CancelInvoiceDialog / RectifyInvoiceDialog (Verifactu requirement 1, see [Verifactu](verifactu/README.md)). The messages point to Anular prendas / Anular factura.
- **Gastos edits**: importe must be a number (comma or dot); the IVA cell accepts only 0, 10 or 21 (`Listado::supportedExpenseIva`, from `sql_lite`'s `expenseIvaIsSummarised`; "21,0" is stored as "21"); the `id` is never editable; fecha must be dd-mm-aaaa and in an open quarter (`quarterIsClosed`, the Formulario facturas rule) and is stored trimmed. A refused edit keeps the cell's value and is reported in the result panel, as is a write SQLite refuses.
- **Locked gastos rows**: a gasto whose fecha is in a quarter closed by Contabilidad is read-only (`MySortFilterProxyModel::flags`, `rowLocked`) and cannot be removed by any path (`MySortFilterProxyModel::removeRows` refuses it; the Delete button says "Fila bloqueada"). Delete, double click and every edit re-read the closed quarters first (`rowLockedNow`). Revert the Contabilidad to change it.
- **Delete** reports a refused `removeRow` instead of "Fila eliminada.".
- **Ingresos view**: `NumberFormatDelegate` on importe, `DecimalCommaDelegate` on the size, `TextColorDelegate` on pagado / estado, `LinkDelegate` on the QR column (opens only `https://`, `isOpenableQrUrl`); hash / csv / xml / cancel-xml / seq / invoice-id / `edit_lock` columns hidden (the gastos `edit_lock` too); the four date columns placed by `placeIngresosDateColumns`.
- **Prices and amounts** (prendas `precio_limpieza` / `precio_plancha`, gastos `importe`): `NumberFormatDelegate` shows two decimals with a comma, edits in comma form and stores a dot (`storedNumber`).

## Search and sort

- One search, no modes (`FilterWidget` over `MySortFilterProxyModel::setSearchText`, once per change). Accents and case are ignored; with several words a row shows when it contains every word, in any searched column and order. `^` or `*` are plain text.
- Searched columns: ingresos up to Observaciones (never the Verifactu metadata); gastos ignore the hidden lock column.
- Sorting is numeric for `n_recibo`, amounts, quantities and sizes (decimal comma read) and by date for the four ingresos dates.

## Loading (`populateTable`)

| Table | Strategy |
|-------|----------|
| `ingresos` | `ORDER BY n_recibo DESC`, first 256 rows, the rest lazily. A search or header click drains the model first (`loadEveryRow`) |
| others | `fetchMore()` until `canFetchMore()` is false |

Afterwards the compact row height is locked (`setDefaultSectionSize(rowHeight(0))`) so lazily fetched rows match.

## InsertNewItem

`QDialog` **Nuevo cliente** (window-modal): Nombre (required), Teléfono fijo, Móvil, Dirección, **Guardar cliente**. A missing name or a failed insert shows in its result panel; it closes itself once saved. Cancelling adds nothing.

## GenListado

Built in code with UiKit. The gastos listing (Generar PDF) has a **Listado** group (Año + Todos los años, Gastos: all / only closed quarters, Agrupar por fecha / proveedor), **Generar listado PDF**, the result panel and Cerrar. The prendas listing (`print_table()`) writes its PDF without showing the dialog.

- The dialog stays open; the PDF is always regenerated (a same-day file is overwritten) and linked in the result panel; no rows is reported there. A failed write is reported in red; cells are HTML-escaped.
- **The PDF follows the active search**: only the rows that match are listed, and the panel says so (`filterNote`).
- The user's table is never reordered: rows are sorted on a copy (`rowsSortedBy`). Supplier groups end in an IMPORTE TOTAL row (`supplierTotalCells`: 7 columns, or 8 with **Cerrado por contabilidad** Si / No, added by "Incluir todos los gastos").
- Amounts are summed in whole cents (`splitGasto`); the year filter ignores spaces around the fecha (`rowYear`). The prendas price list reads comma prices and prints an empty or zero price blank.
- Output: `AppSettings::listadosPrendasPath()` (`<reports.root>/Listados/Prendas`) and `listadosGastosPath()` (`<reports.root>/Listados/Gastos`), created on demand; both through `ReportHtml::writePdf`.

## Dependencies

`listado` links `tableview` PUBLIC and `sql_lite`, `appsettings`, `reporthtml`, `uikit` PRIVATE (`src/listado/CMakeLists.txt`).
