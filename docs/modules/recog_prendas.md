# RecogPrendas (`src/recog_prendas/`)

"Garment pickup" panel. Loads `ingresos` rows into a filterable table. Handles payment marking, pickup marking, observation editing, size/price editing, and garment row splitting.

## Source files

- `src/recog_prendas/recog_prendas.h/cpp`

## Key interface

```cpp
RecogPrendas *ui = new RecogPrendas(this);
ui->db = db;
ui->show();
```

## Operations

| Operation | `UpdateDBop` value | What it writes |
|-----------|-------------------|----------------|
| Mark paid | `PAY_YES` / `PAY_NO` | `pagado`, `fecha_pago` |
| Mark picked up | `PKU_YES` / `PKU_NO` | `estado`, `fecha_recogida` |
| Edit observation | `OBSV` | `observaciones` |
| Edit size + price | `SIZE_AND_PRICE` | `size`, `importe` |
| Split garment row | `SEPARATE_GARM` | Inserts a new row with `genHash16()`; decrements quantity on original |

## Search

Triggered by `on_pb_search_clicked()` (also `on_le_search_returnPressed()`). Input is classified by content:

| Input type | Detection | SQL / behaviour |
|------------|-----------|-----------------|
| Ticket number | All digits, length < 9 | `WHERE n_recibo = :n` |
| Phone number | All digits, length ≥ 9 | `WHERE tel_fijo LIKE :p OR movil LIKE :p` on `clientes`; then `WHERE cliente = :name` |
| Date (`dd/MM/yyyy` or `dd-MM-yyyy`) | `QDate::fromString` succeeds | `WHERE <date_column> = :date` |
| Client name | Default (simple text, not a date) | `SELECT * FROM ingresos` (all rows) filtered client-side by `MySortFilterProxyModel::setNormalizedFilter(text, INGRESOS_COL_CLIENTE)` — **diacritic-insensitive**: typing "garcia" matches "García" |

Name search loads all rows because SQLite LIKE is ASCII-only and cannot match accented characters. The proxy model's `removeDiacritics()` + `toLower()` normalization handles the matching client-side.

## `ingresos` column indices

Defined once in `sql_lite.h` as `INGRESOS_COL_<UPPER_DB_COLUMN_NAME>` so the constant name matches the SQL column verbatim. All consumers (`recog_prendas`, `imprimir`, `listado`, `mainwindow`, `mysortfilterproxymodel`, `add_garment`) share this one set — prior to the consolidation each module carried its own near-duplicate macro block and they had already started to diverge (the v8.0 milestone caught a `verifactu_xml`/`verifactu_hash` write being silently dropped on the pay-at-pickup path because one of the duplicate copies of `updateTicketVerifactuFields()` had missed the new columns).

| Constant | Index | DB column |
|----------|-------|-----------|
| `INGRESOS_COL_N_RECIBO` | 0 | n_recibo |
| `INGRESOS_COL_CLIENTE` | 1 | cliente |
| `INGRESOS_COL_FECHA_RECEPCION` | 2 | fecha_recepcion |
| `INGRESOS_COL_FECHA_PAGO` | 3 | fecha_pago |
| `INGRESOS_COL_FECHA_RECOGIDA` | 4 | fecha_recogida |
| `INGRESOS_COL_IMPORTE` | 5 | importe |
| `INGRESOS_COL_PAGADO` | 6 | pagado |
| `INGRESOS_COL_ESTADO` | 7 | estado |
| `INGRESOS_COL_CANTIDAD` | 8 | cantidad |
| `INGRESOS_COL_PRENDA` | 9 | prenda |
| `INGRESOS_COL_SIZE` | 10 | size |
| `INGRESOS_COL_SERVICIO` | 11 | servicio |
| `INGRESOS_COL_OBSERVACIONES` | 12 | observaciones |
| `INGRESOS_COL_EDIT_LOCK` | 13 | edit_lock |
| `INGRESOS_COL_HASH` | 14 | hash |
| `INGRESOS_COL_VERIFACTU_CSV` | 15 | verifactu_csv |
| `INGRESOS_COL_VERIFACTU_TIMESTAMP` | 16 | verifactu_timestamp |
| `INGRESOS_COL_VERIFACTU_ESTADO` | 17 | verifactu_estado |
| `INGRESOS_COL_VERIFACTU_ERROR` | 18 | verifactu_error |
| `INGRESOS_COL_VERIFACTU_URL_QR` | 19 | verifactu_url_qr |
| `INGRESOS_COL_VERIFACTU_XML` | 20 | verifactu_xml |
| `INGRESOS_COL_VERIFACTU_HASH` | 21 | verifactu_hash |
| `INGRESOS_COL_VERIFACTU_RECTIFIES_N_RECIBO` | 22 | verifactu_rectifies_n_recibo |
| `INGRESOS_COL_VERIFACTU_RECTIFICATION_TYPE` | 23 | verifactu_rectification_type |
| `INGRESOS_COL_VERIFACTU_INVOICE_SEQ` | 24 | verifactu_invoice_seq |

Columns 13–23 are loaded by the `SELECT *` query but hidden from the `QTableView` via `setColumnHidden()`. They are read only by `updateDb()` (hash, edit_lock) and `on_pb_verifactu_clicked()` (verifactu_*).

## Verifactu info button (`pb_verifactu`)

A "Verifactu" button is shown alongside the other action buttons. It is **enabled only when the selected row has a non-empty `verifactu_estado`** (i.e. the ticket has a Verifactu state — `PENDIENTE`, `ENVIADA`, `ERROR`, or `ANULADA`). Legacy rows from before Verifactu (NULL/empty) keep the button disabled. Clicking it opens a `QDialog` showing:

- Estado (`PENDIENTE` / `ENVIADA` / `ERROR` / `ANULADA`)
- CSV security code
- Submission timestamp
- Error description (if any)
- Clickable "Abrir en AEAT" link to the `verifactu_url_qr` validation URL

When `estado == "ERROR"` and Verifactu is configured, the dialog also shows a **"Reintentar envío a AEAT"** button. Clicking it:

1. Closes the dialog.
2. Queries `SUM(importe)` for the ticket from `ingresos` to recompute the total.
3. Calls `VerifactuIntegration::submitSimplifiedInvoice()` with the original `n_recibo`, `fecha_recepcion`, and the recomputed tax base.
4. Updates **all rows** for that `n_recibo` with the new `verifactu_*` values (`ENVIADA` or `ERROR`).
5. Refreshes the table view and shows a success/failure message.

Implemented in `retryVerifactuSubmit(ticketNum, invoiceDate)` — a private method called from a lambda in `on_pb_verifactu_clicked()`.

Note: the QR image is not stored in the DB; staff can access it via the AEAT validation link.

## Button enable/disable behaviour

All action buttons start **disabled**. They are enabled when a row is clicked in the table:

| Button | Enabled when |
|--------|-------------|
| `pb_state`, `pb_pay_all`, `pb_pku_all`, `pb_separ_garm`, `pb_print` | Any row is selected |
| `pb_verifactu` | Selected row has `verifactu_estado` non-empty |
| `pb_payment` | **Never** — kept disabled because the per-garment toggle path has been deprecated in 8.5; payment now goes through `PayDialog` opened from `pb_pay_all`, which submits one Verifactu invoice per payment event with `InvoiceID = "<n_recibo>-<seq>"` and avoids the duplicate-InvoiceID rejection the old per-garment path would have caused. |

`resetAllContents()` (called on search and reset) disables all buttons. `on_tableView_clicked()` maps the proxy index to source via `proxyModel->mapToSource()` and then calls `selectSourceRow(sourceRow, sourceCol)` which stores `rowClickedCell` as a **source-model row** and re-enables the row-selection group (excluding `pb_payment`); `updateRowClickedToFields()` conditionally enables `pb_verifactu`.

## Partial-payment dialog (8.5+)

`pb_pay_all` opens `PayDialog` (`src/recog_prendas/pay_dialog.{h,cpp}`) with the clicked ticket pre-loaded. The dialog lists every unpaid row of the ticket with a per-row checkbox pre-checked; the operator unticks the rows the client is not paying this time, sees the live "Total seleccionado" update, and clicks Cobrar. PayDialog then:

1. Calls `nextVerifactuInvoiceSeq(db, ticketNum)` to get the next free `seq` for this ticket.
2. Builds `InvoiceID = "<n_recibo>-<seq>"` and fires `VerifactuIntegration::submitSimplifiedInvoiceAsync()` with a 5 s bounded wait.
3. On success: UPDATEs the chosen rows `pagado='SI'`, `fecha_pago=:date`, `verifactu_invoice_seq=:seq`, calls `updateTicketVerifactuFieldsForSeq()` for the AEAT metadata, and prints the partial factura via `Imprimir` with `invoiceSeq` set so only the paid subset is on the print.
4. On AEAT timeout / config error: still persists the payment locally and prints a paid recibo without QR.

Remaining unpaid rows of the same ticket stay unpaid; a later payment event picks up `seq = max(seq) + 1`. `pb_print` reads the clicked row's `verifactu_invoice_seq` and forwards it to `printFactura(ticketNum, askSecondCopy, invoiceSeq)` so a reprint matches the AEAT-side invoice exactly.

## Header-click sorting

`ui->tableView->setSortingEnabled(true)` is set on every search (in `on_pb_search_clicked` after `setModel(proxyModel)`), so any column header can be clicked to re-sort the result. The sort uses `MySortFilterProxyModel::lessThan()`, which already special-cases the three date columns (parsed as `dd-MM-yyyy`) and `importe` (parsed as float) so they sort numerically rather than as strings. Default sort on every new search is `n_recibo` descending.

## Notes

- All DB updates identify the target row by `n_recibo` + `hash` pair — safe under sort/filter.
- Accounting-locked rows (`edit_lock=1`) cannot be modified; `updateDb()` checks this.
- `pb_pay_all` opens `PayDialog` for the clicked ticket (single ticket scope). `pb_pku_all` still loops `sqlQueryModel->rowCount()` — for name searches (`SELECT *` loads the whole table) this means it would mark every ticket in the DB as picked up. Tracked in Open Non-Blocking Issues for 8.6.
- Total price display (`le_total_price`) sums from the proxy model rows, not the raw SQL result, so it always reflects the filtered set.
