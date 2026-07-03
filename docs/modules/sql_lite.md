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
| `garmentImporte(quantityText, sizeText, unitPrice)` | `double` | Pure price math (no DB): comma-normalised `quantity * unitPrice`, times a non-zero size factor, clamped to `>= 0`. Shared by `MainWindow`/`AddGarment` `setGarmentPrice` |
| `verifactuInvoiceId(nRecibo, seq)` | `QString` | Pure: AEAT InvoiceID for a payment event — bare `n_recibo` for seq 0, `<n_recibo>-<seq>` otherwise. Single source of truth used at submit / persist / cancel / reprint |
| `verifactuDisplayInvoiceId(invoiceIds, fallback)` | `QString` | Pure: the InvoiceID to print for a ticket — first non-empty literal among the rows' `verifactu_invoice_id`, else the fallback (bare `n_recibo`). Backs `Imprimir::displayInvoiceId()`; scans (not row 0) so an unpaid row 0 of a multi-event ticket doesn't hide the real id |
| `pendingVerifactuEvents(db, floorIso)` | `QVector<PendingVerifactuEvent>` | Startup-recovery feed: one `{n_recibo, seq, fechaPago, cliente, importe}` per `(n_recibo, verifactu_invoice_seq)` still PENDIENTE/empty whose `fecha_recepcion` is on or after `floorIso`, each with its own `SUM(importe)`. Grouping by seq (no seq=0 filter) surfaces partial-pay events for recovery. `fechaPago` is the original AEAT submission date a retry must reuse (date is part of the invoice identity); the floor gates on `fecha_recepcion`. Ordered `n_recibo DESC, seq`. Drives `PendingSubmitsDialog` |
| `removeSpecialChars(str)` | `QString` | Pure: strip diacritics/non-Latin1 marks (NFD → Latin-1 → drop `?`) for accent-insensitive name matching; case preserved. Used by `MainWindow::removeSpecialChar` |
| `selectFromWhereLike(db, item, table, col, search, exact, print)` | `QString` | Generic single-value lookup |
| `searchItemFromClient(db, item, client, print)` | `QString` | Lookup a field by client name in `ingresos` |
| `readClientPhones(db, client)` | `QStringList` | `{tel_fijo, movil}` for an exact client name in one query (both empty if not found) |
| `updateItemToClient(db, column, item, client)` | `bool` | Update a field for a client in `clientes` |
| `addNewClient(db, name, telFijo, direccion, movil)` | `bool` | Insert a row into `clientes` |
| `totalPriceBetweenDates(db, table, start, end, iva)` | `float` | Sum of `importe` for a date range and IVA rate. For `ingresos`: excludes `verifactu_estado = 'ANULADA'` rows. Both tables use `[start, end)` half-open interval |
| `annualAccountingByQuarter(db, year)` | `QuarterlyAccountingTotals` | One grouped query per table for the whole year, bucketed by quarter (`(month+2)/3`, index 0 = Q1): ingresos `SUM(importe)` + `COUNT(DISTINCT n_recibo)` under the same `pagado='SI'`/ANULADA/RECTIFICADA/date filters as `totalPriceBetweenDates`; gastos `SUM(importe)` per quarter×iva (10/21/0) + a per-quarter row count over all rates. Powers the annual Contabilidad report (24 per-quarter scans → 2). Sums in SQL, so it does **not** raise the comma-decimal dialog the per-row helpers do |
| `readLockForMonthAndYear(db, table, month, year)` | `int` | `1` if the month's rows are accounting-locked, `0` if open **or the month has no rows** (a valid empty SELECT returns 0 here — it does not distinguish "no data"); `2` only on a query error. Use `readLockForQuarter` when "no data" must be distinguished |
| `readLockForQuarter(db, table, quarter, year)` | `int` | Quarter-wide lock (reads all three months in one query): `1` if any row of the quarter is locked, `0` if it has data but is open, `2` if no rows. Avoids the last-month-only blind spot of `readLockForMonthAndYear` |
| `updateLockForMonth(db, value, month, year)` | `void` | Lock (`1`) or unlock (`0`) a month+year in both `ingresos` and `gastos` |
| `updateComasInDecimalData(db, table, item)` | `int` | Replace comma decimal separators with dots |
| `insertNewItemToTable(db, items, table)` | `void` | Generic row insert |
| `genHash16()` | `QString` | Generate a 16-char alphanumeric deduplication hash |
| `updateTicketPayment(db, nRecibo, hash, fechaPago, pagado)` | `bool` | RecogPrendas seam: set `fecha_pago` + `pagado` on the `(n_recibo, hash)` row (PAY_YES; PAY_NO passes empty `fechaPago`) |
| `updateTicketPickup(db, nRecibo, hash, fechaRecogida, estado)` | `bool` | RecogPrendas seam: set `fecha_recogida` + `estado` (PKU_YES; PKU_NO passes empty `fechaRecogida`) |
| `markTicketPickedUp(db, nRecibo, fechaRecogida)` | `bool` | "Recoger todo" seam: one `UPDATE ... SET estado='Recogido'` over the whole ticket, with `AND estado != 'Anulado'` so a voided garment is never revived to Recogido |
| `updateTicketObservations(db, nRecibo, hash, observaciones)` | `bool` | RecogPrendas seam: set `observaciones` (OBSV) |
| `updateTicketSizeAndPrice(db, nRecibo, hash, size, importe)` | `bool` | RecogPrendas seam: set `size` + `importe` (SIZE_AND_PRICE) |
| `updateGarmentQtyAndImporte(db, nRecibo, hash, cantidad, importe)` | `bool` | RecogPrendas seam: set `cantidad` + `importe` on the reduced row (SEPARATE_GARM 1/2) |
| `insertGarmentRow(db, IngresoGarmentRow)` | `bool` | Insert one `ingresos` garment row. Shared by RecogPrendas SEPARATE_GARM (split-off row, `verifactuEstado=""` → legacy/NotSubmitted; re-submitting it would duplicate the ticket's AEAT InvoiceID) and MainWindow `saveTicket` (fresh ticket row, `verifactuEstado="PENDIENTE"` so the async submit can patch it). `verifactuEstado` is the only verifactu_* column written |
| `ticketVerifactuEstado(db, nRecibo)` | `QString` | `verifactu_estado` of the ticket's first row (empty if none). Read by the PAY_YES pay-all dedup after a payment write |
| `garmentIsLocallyVoidable(pagado, verifactuEstado)` | `bool` | Pure guard for VoidGarmentsDialog: true only when a row is unpaid (`pagado != "SI"`) AND never sent to AEAT (`verifactu_estado` PENDIENTE/empty). A paid/ENVIADA row was registered at AEAT and must be cancelled via CancelInvoiceDialog instead |
| `voidGarmentRow(db, nRecibo, hash)` | `bool` | VoidGarmentsDialog seam: void one garment in place — `UPDATE ingresos SET estado='Anulado', verifactu_estado='ANULADA'` keyed by `(n_recibo, hash)`. `pagado` is left untouched. Caller gates the row through `garmentIsLocallyVoidable` first |
| `ticketHasPaidGarment(db, nRecibo)` | `bool` | True if the ticket has any `pagado='SI'` row. AddGarment uses it to refuse appending garments to a paid (already AEAT-submitted) ticket |

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
