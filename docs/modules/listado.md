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
- Auto-resize window to table content

## Dependencies (CMake)

`listado` links against the `tableview` library as PUBLIC, which exposes `FilterWidget`, `MySortFilterProxyModel`, `TableView`, `NumberFormatDelegate`, and `TextColorDelegate` to consumers of `listado`.

## Sub-classes

### InsertNewItem
`QDialog` for adding a new client row. Fields: name, landline, mobile, address. Calls `insertNewItemToTable(db, ..., "clientes")` on accept.

### GenListado
`QDialog` for generating garment or expense PDF reports. Configurable by year and grouping mode (by date or by supplier). Generates HTML and renders to PDF via `QTextDocument` + `QPrinter`. Output paths are read from `AppSettings` (`listadosPrendasPath`, `listadosGastosPath`).
