# Listado (`src/listado/`)

Generic list-view window. Set `tableName` at runtime to display any DB table. Used by `MainWindow` for `ingresos`, `gastos`, `prendas`, `clientes`, `proveedores`, and `servicios`.

A `QMainWindow` built in code with the shared [UiKit](uikit.md) style (no hand-copied form code, no fixed 10 / 26 / 8 pt fonts): a heading (`lbl_title`, set by MainWindow), an explanation panel worded per table, an action row - **Añadir fila**, **Eliminar fila**, **Generar PDF** (each shown only where the table supports it), the search box and **Actualizar** - the table (one point under the app font, as it is wide), and a result panel where every message appears: a new gasto points to Formulario facturas, a row of a closed quarter cannot be edited, nothing selected to delete, a row added / deleted, a PDF written. The menus and their shortcuts (Ctrl+A / Ctrl+N / Ctrl+D / Ctrl+P) remain and trigger the same actions; only deleting a row still asks for confirmation.

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
    void populateClientes();  // emitted after editing the clients table
    void populatePrendas();   // emitted after editing the garments table
```

`MainWindow` connects to these to refresh its client and garment comboboxes after edits.

## Features

- Add / delete rows via the buttons, the menu actions or the table's context menu
- Text filter via `FilterWidget` (backed by `MySortFilterProxyModel`)
- **Diacritic-insensitive search**: typing "garcia" matches "García". Implemented via `MySortFilterProxyModel::setNormalizedFilter` called from `textFilterChanged()`.
- PDF export via `GenListado` dialog (`actionGenerar_pdf_con_el_listado`)
- Inline cell editing via double-click for editable tables; locked rows (`edit_lock=1`) report "Edición bloqueada" in the result panel. **`ingresos` is read-only in this view** — `setEditTriggers(NoEditTriggers)` is applied so the operator cannot bypass AEAT / the chained Huella / the accounting lock by editing cells directly. All `ingresos` changes must go through RecogPrendas / CancelInvoiceDialog / RectifyInvoiceDialog (Verifactu Req. 1, Art. 8.1 RD 1007/2023).
- Auto-resize window to table content, capped at the current screen's available width (`screen()->availableGeometry().width()`) so the window never extends off-screen

## Data loading strategy (`populateTable`)

`QSqlTableModel` fetches rows lazily in batches of 256. Two strategies are used:

| Table | Strategy | Reason |
|-------|----------|--------|
| `ingresos` | SQL-level `ORDER BY n_recibo DESC` — first batch is the most recent 256 rows; older rows load lazily as the user scrolls | `ingresos` can have thousands of rows; loading all upfront is slow |
| All others | `model->fetchMore()` loop until `canFetchMore()` is false — all rows loaded immediately | Smaller tables where full load is fast and filters need all data |

After the initial load `verticalHeader()->setDefaultSectionSize(rowHeight(0))` locks the compact row height so lazily-fetched rows match the initially-sized rows and don't expand unexpectedly on scroll.

## Dependencies (CMake)

`listado` links against the `tableview` library as PUBLIC, which exposes `FilterWidget`, `MySortFilterProxyModel`, `TableView`, `NumberFormatDelegate`, and `TextColorDelegate` to consumers of `listado`.

## Sub-classes

### InsertNewItem
`QDialog` **Nuevo cliente** (UiKit style, window-modal) opened by Añadir fila in Listado de clientes: Nombre (required), Teléfono fijo, Móvil, Dirección and **Guardar cliente**; a missing name or a failed insert (`insertNewItemToTable` now returns whether it wrote) is shown in its result panel, and it closes itself once the client is saved. Cancelling adds nothing.

### GenListado
`QDialog` for generating garment or expense PDF reports, built in code with the shared [UiKit](uikit.md) style (no `.ui`). The expense listing (Listado de gastos → Generar PDF) shows a **Listado** group (Año + Todos los años, Gastos: all / only closed quarters, Agrupar por fecha / proveedor), **Generar listado PDF**, the result panel and Cerrar; the dialog stays open, the PDF is always regenerated (an earlier one of the same day is overwritten) and linked in the result panel, and a configuration without rows is reported there instead of a message box. The garment listing (`print_table()`) writes its PDF without showing the dialog. Both render through `ReportHtml::writePdf`. Output paths are read from `AppSettings::listadosPrendasPath()` (→ `<reports.root>/Listados/Prendas`) and `AppSettings::listadosGastosPath()` (→ `<reports.root>/Listados/Gastos`); both call `QDir::mkpath()` on demand.
