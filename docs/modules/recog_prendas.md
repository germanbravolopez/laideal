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
| Client name | Default (simple text, not a date) | `SELECT * FROM ingresos` (all rows) filtered client-side by `MySortFilterProxyModel::setNormalizedFilter(text, TABLE_CLIENT)` — **diacritic-insensitive**: typing "garcia" matches "García" |

Name search loads all rows because SQLite LIKE is ASCII-only and cannot match accented characters. The proxy model's `removeDiacritics()` + `toLower()` normalization handles the matching client-side.

## Table column indices

Defined in `recog_prendas.h`. `TABLE_HASH` (14) is defined in `imprimir.h` which is included transitively.

| Constant | Index | Column |
|----------|-------|--------|
| `TABLE_TICKET` | 0 | n_recibo |
| `TABLE_CLIENT` | 1 | cliente |
| `TABLE_DATE_RCP` | 2 | fecha_recepcion |
| `TABLE_DATE_PAY` | 3 | fecha_pago |
| `TABLE_DATE_PKU` | 4 | fecha_recogida |
| `TABLE_PRICE` | 5 | importe |
| `TABLE_IS_PAYED` | 6 | pagado |
| `TABLE_STATE` | 7 | estado |
| `TABLE_QUANTITY` | 8 | cantidad |
| `TABLE_GARMENT` | 9 | prenda |
| `TABLE_SIZE` | 10 | size |
| `TABLE_SERVICE` | 11 | servicio |
| `TABLE_OBSERV` | 12 | observaciones |
| `TABLE_EDIT_LOCK` | 13 | edit_lock |
| `TABLE_HASH` *(imprimir.h)* | 14 | hash |
| `TABLE_VERIFACTU_CSV` | 15 | verifactu_csv |
| `TABLE_VERIFACTU_TIMESTAMP` | 16 | verifactu_timestamp |
| `TABLE_VERIFACTU_ESTADO` | 17 | verifactu_estado |
| `TABLE_VERIFACTU_ERROR` | 18 | verifactu_error |
| `TABLE_VERIFACTU_URL_QR` | 19 | verifactu_url_qr |

Columns 14–19 are loaded by the `SELECT *` query but hidden from the `QTableView` via `setColumnHidden()`. They are read only by `updateDb()` (hash) and `on_pb_verifactu_clicked()` (verifactu_*).

## Verifactu info button (`pb_verifactu`)

A "Verifactu" button is shown alongside the other action buttons. It is **enabled only when the selected row has a non-empty `verifactu_estado`** (i.e. the ticket was submitted to AEAT or attempted). Clicking it opens a `QDialog` showing:

- Estado (`ENVIADA` / `ERROR`)
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
| `pb_payment`, `pb_state`, `pb_pay_all`, `pb_pku_all`, `pb_separ_garm`, `pb_print` | Any row is selected |
| `pb_verifactu` | Selected row has `verifactu_estado` non-empty |

`resetAllContents()` (called on search and reset) disables all buttons. `on_tableView_clicked()` re-enables the first group; `updateRowClickedToFields()` conditionally enables `pb_verifactu`.

## Notes

- All DB updates identify the target row by `n_recibo` + `hash` pair — safe under sort/filter.
- Accounting-locked rows (`edit_lock=1`) cannot be modified; `updateDb()` checks this.
- `PAY_ALL` and `PKU_ALL` buttons mark all visible rows paid or picked up in one operation.
- Total price display (`le_total_price`) sums from the proxy model rows, not the raw SQL result, so it always reflects the filtered set.
