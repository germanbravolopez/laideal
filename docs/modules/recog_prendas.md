# RecogPrendas (`src/recog_prendas/`)

"Garment pickup" panel. Loads `ingresos` rows into a filterable table. Handles payment marking, pickup marking, observation editing, quantity/service/size/price editing, and garment row splitting.

## Source files

- `src/recog_prendas/recog_prendas.h/cpp`

## Key interface

```cpp
RecogPrendas *ui = new RecogPrendas(db, this);  // db injected via constructor
ui->show();
```

## Operations

| Operation | `UpdateDBop` value | What it writes |
|-----------|-------------------|----------------|
| Mark paid | `PAY_YES` / `PAY_NO` | `pagado`, `fecha_pago` |
| Mark picked up | `PKU_YES` / `PKU_NO` | `estado`, `fecha_recogida` |
| Edit observation | `OBSV` | `observaciones` |
| Edit size + price | `SIZE_AND_PRICE` | `size`, `importe` |
| Edit quantity | `QTY` | `cantidad`, `importe` (re-priced via `garmentImporte`) |
| Change service | `SERVICE` | `servicio`, `importe` (re-priced against the new service's unit price) |
| Edit importe (manual) | `PRICE` | `importe` (comma-normalised manual override; `size` left as-is) |
| Split garment row | `SEPARATE_GARM` | Inserts a new row with `genHash16()`; decrements quantity on original |

`updateDb()` keeps the business rules (edit_lock guard, blocked-quarter check, the post-PAY_YES Verifactu-submit decision) and reads its values off the dialog widgets, but each actual write delegates to a pure `sql_lite` seam keyed by `(n_recibo, hash)` — `updateTicketPayment` / `updateTicketPickup` / `updateTicketObservations` / `updateTicketSizeAndPrice`, `updateGarmentQtyAndImporte` (QTY + the split), `updateGarmentServiceAndImporte` (SERVICE) + `insertGarmentRow(IngresoGarmentRow)` for the split, and `ticketVerifactuEstado` for the pay-all dedup read-back — so the DB writes are unit-tested in `test_sql_lite` (the dialog/UI wiring itself stays integration-level).

## Pre-payment editing of quantity / service / importe

Quantity (`le_qty`, constrained to integers by a `QIntValidator(1, 9999)` set in `initialSettings()` so a non-integer count can't be typed), importe (`le_price`), size (`le_size`) and the service picker (`cb_servic`, a `QComboBox` offering `Limp.` / `Plan.` - it replaced the old read-only `le_servic` line edit) are editable **only before payment**: `updateRowClickedToFields()` computes `priceEditable = !isAnulado && !isPaid && !editLock` and read-onlys / disables all four accordingly, and each `updateDb()` case re-checks `!editLock && pagado == "NO"` as defense-in-depth. Editing quantity or changing the service re-prices the row automatically (`importe = qty * unitPrice * size` via `garmentImporte`), while `le_price` remains a manual override applied last. The service combo writes on the `activated(int)` signal (user interaction only) so `setCurrentIndex()` during row-load never fires a spurious write; a legacy `servicio` value not in the list maps to `findText() == -1` and shows blank rather than a wrong service.

A locally voided garment (`estado == "Anulado"`, set by `VoidGarmentsDialog`) is treated as **read-only** here: `updateRowClickedToFields()` disables `pb_state` / `pb_pay_all` / `pb_pku_all` / `pb_separ_garm` and read-onlys `le_size` / `le_price` for such a row, and `updateDb()` early-returns on an Anulado row as defense-in-depth so no write path can revive or charge it. **Observations are the one exception**: `le_obsv` stays editable and the `OBSV` op is exempt from the `updateDb()` early-return, so staff can record why the garment was anulada (or add later notes) on a voided row.

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
| `pb_state`, `pb_pay_all`, `pb_pku_all`, `pb_separ_garm` | A row is selected AND it is **not** `Anulado` (a voided garment is read-only) |
| `pb_print` | A row is selected |
| `pb_verifactu` | Selected row has `verifactu_estado` non-empty |
| `pb_payment` | **Never** — kept disabled because the per-garment toggle path has been deprecated in 8.5; payment now goes through `PayDialog` opened from `pb_pay_all`, which submits one Verifactu invoice per payment event with `InvoiceID = "<n_recibo>-<seq>"` and avoids the duplicate-InvoiceID rejection the old per-garment path would have caused. |

`resetAllContents()` (called on search and reset) disables all buttons. `on_tableView_clicked()` maps the proxy index to source via `proxyModel->mapToSource()` and then calls `selectSourceRow(sourceRow, sourceCol)` which stores `rowClickedCell` as a **source-model row** and calls `updateRowClickedToFields()` — the single source of truth for the per-row button enables: it enables the row-selection group (excluding `pb_payment`, and excluding all edit buttons for an `Anulado` row) and conditionally enables `pb_verifactu`.

## Partial-payment dialog (8.5+)

`pb_pay_all` opens `PayDialog` (`src/recog_prendas/pay_dialog.{h,cpp}`) with the clicked ticket pre-loaded. The dialog lists every unpaid row of the ticket with a per-row checkbox pre-checked; the operator unticks the rows the client is not paying this time, sees the live "Total seleccionado" update, and clicks Cobrar. PayDialog then:

1. Calls `nextVerifactuInvoiceSeq(db, ticketNum)` to get the next free `seq` for this ticket.
2. Builds `InvoiceID = "<n_recibo>-<seq>"` and fires `VerifactuIntegration::submitSimplifiedInvoiceAsync()` with a 5 s bounded wait.
3. On success: UPDATEs the chosen rows `pagado='SI'`, `fecha_pago=:date`, `verifactu_invoice_seq=:seq`, calls `updateTicketVerifactuFields(db, n_recibo, result, seq)` for the AEAT metadata (single seq-scoped helper used by both save-time and PayDialog paths), and prints the partial factura via `Imprimir` with `invoiceSeq` set so only the paid subset is on the print. A **single** copy (the customer factura) is auto-printed - no second business copy; reprint from RecogPrendas (`pb_print`) if another is needed.
4. On AEAT timeout / config error: still persists the payment locally and prints a paid recibo without QR.

Remaining unpaid rows of the same ticket stay unpaid; a later payment event picks up `seq = max(seq) + 1`. `pb_print` reads the clicked row's `verifactu_invoice_seq` and forwards it to `printFactura(ticketNum, askSecondCopy, invoiceSeq)` so a reprint matches the AEAT-side invoice exactly.

## Header-click sorting

`ui->tableView->setSortingEnabled(true)` is set on every search (in `on_pb_search_clicked` after `setModel(proxyModel)`), so any column header can be clicked to re-sort the result. The sort uses `MySortFilterProxyModel::lessThan()`, which already special-cases the three date columns (parsed as `dd-MM-yyyy`) and `importe` (parsed as float) so they sort numerically rather than as strings. Default sort on every new search is `n_recibo` descending.

## Notes

- All DB updates identify the target row by `n_recibo` + `hash` pair — safe under sort/filter.
- Accounting-locked rows (`edit_lock=1`) cannot be modified; `updateDb()` checks this.
- `pb_pay_all` opens `PayDialog` for the clicked ticket (single-ticket scope); `PayDialog::loadTicket` skips `Anulado` rows so a voided garment can never be charged/submitted. `pb_pku_all` is likewise single-ticket scoped: it calls `sql_lite::markTicketPickedUp` which runs one `UPDATE ... WHERE n_recibo = :n AND estado != 'Anulado'` (no per-row loop over the model), so a name search no longer marks every ticket in the DB as Recogido and a voided garment is never revived.
- Total price display (`le_total_price`) sums from the proxy model rows, not the raw SQL result, so it always reflects the filtered set.
