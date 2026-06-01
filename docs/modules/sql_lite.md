# sql_lite (`src/sql_lite/`)

Stateless free-function database API. Every module includes `sql_lite.h`. Functions open and close the connection internally — callers pass a `QSqlDatabase &db` reference owned by their window.

## Source files

- `src/sql_lite/sql_lite.h`
- `src/sql_lite/sql_lite.cpp`

## DB path

```cpp
#define DB_PATH dbPath()   // expands to the runtime-configured path
```

Set once at startup by `main()`:
```cpp
AppSettings *settings = AppSettings::instance();
settings->load();
setDbPath(settings->dbPath());
```

The actual path is stored in `~/.laideal_settings.json` under the `db.path` key and is configured via Archivo → Configuración. If the path is empty or the file does not exist, `main()` opens `SettingsDialog` before constructing `MainWindow`.

## Function reference

| Function | Returns | Purpose |
|----------|---------|---------|
| `migrateDatabase(db)` | `void` | Adds 5 `verifactu_*` columns to `ingresos` via `ALTER TABLE ADD COLUMN`; idempotent — silently skips if columns exist |
| `readMaxValueInColumnFromTable(db, column, table)` | `int` | Max integer value in a column |
| `readMaxNMinYearInColumnFromTable(db, maxNMin, column, table)` | `int` | Max (`true`) or min (`false`) year from a date-string column |
| `readColumnFromTable(db, column, table, orderByColumn)` | `QStringList` | All values in a column |
| `readGarmentPrice(db, garment, service)` | `float` | Unit price for a garment + service combination |
| `selectFromWhereLike(db, item, table, col, search, exact, print)` | `QString` | Generic single-value lookup |
| `searchItemFromClient(db, item, client, print)` | `QString` | Lookup a field by client name in `ingresos` |
| `updateItemToClient(db, column, item, client)` | `bool` | Update a field for a client in `clientes` |
| `addNewClient(db, name, telFijo, direccion, movil)` | `bool` | Insert a row into `clientes` |
| `totalPriceBetweenDates(db, table, start, end, iva)` | `float` | Sum of `importe` for a date range and IVA rate. For `ingresos`: excludes `verifactu_estado = 'ANULADA'` rows. Both tables use `[start, end)` half-open interval |
| `readLockForMonthAndYear(db, table, month, year)` | `int` | `1` if quarter is accounting-locked, `0` if open |
| `updateLockForMonth(db, value, month, year)` | `void` | Lock (`1`) or unlock (`0`) a month+year in both `ingresos` and `gastos` |
| `updateComasInDecimalData(db, table, item)` | `int` | Replace comma decimal separators with dots |
| `insertNewItemToTable(db, items, table)` | `void` | Generic row insert |
| `genHash16()` | `QString` | Generate a 16-char alphanumeric deduplication hash |

## Usage pattern

```cpp
db.open();
QSqlQuery q(db);
q.prepare("SELECT col FROM table WHERE id = :id");
q.bindValue(":id", value);
q.exec();
db.close();
```

Always use named parameters (`:param`). Never build SQL by string concatenation — SQL injection risk.

## Accounting lock logic

`readLockForMonthAndYear()` maps a month+year to its quarter and checks `edit_lock` in `ingresos`. Returns `1` if that quarter is locked. Called by `MainWindow`, `RecogPrendas`, `AddGarment`, and `Facturas` before any write.
