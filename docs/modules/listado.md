# Listado (`src/listado/`)

Generic list-view window. Set `tableName` at runtime to display any DB table. Used by `MainWindow` for `ingresos`, `gastos`, `prendas`, `clientes`, `proveedores`, and `servicios`.

## Source files

| File | Purpose |
|------|---------|
| `src/listado/listado.h/cpp` | Main list viewer |
| `src/listado/insertnewitem.h/cpp` | Dialog for inserting a new row into `clientes` |
| `src/listado/genlistado.h/cpp` | Dialog for generating garment/expense PDF reports |

## Key interface

```cpp
Listado *ui_listado = new Listado(this);
ui_listado->db = db;
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

- Add / delete rows via menu actions
- Text filter via `FilterWidget` (backed by `MySortFilterProxyModel`)
- **Diacritic-insensitive search**: typing "garcia" matches "García". Implemented via `MySortFilterProxyModel::setNormalizedFilter` called from `textFilterChanged()`.
- PDF export via `GenListado` dialog (`actionGenerar_pdf_con_el_listado`)
- Inline cell editing via double-click; locked rows (`edit_lock=1`) show a warning
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
`QDialog` for adding a new client row. Fields: name, landline, mobile, address. Calls `insertNewItemToTable(db, ..., "clientes")` on accept.

### GenListado
`QDialog` for generating garment or expense PDF reports. Configurable by year and grouping mode (by date or by supplier). Generates HTML and renders to PDF via `QTextDocument` + `QPrinter`. Output paths are read from `AppSettings::listadosPrendasPath()` (→ `<reports.root>/Listados/Prendas`) and `AppSettings::listadosGastosPath()` (→ `<reports.root>/Listados/Gastos`); both call `QDir::mkpath()` on demand.
