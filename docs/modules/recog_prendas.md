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

## Table column indices (`recog_prendas.h`)

`TABLE_TICKET=0`, `TABLE_CLIENT=1`, `TABLE_DATE_RCP=2`, `TABLE_DATE_PAY=3`, `TABLE_DATE_PKU=4`, `TABLE_PRICE=5`, `TABLE_IS_PAYED=6`, `TABLE_STATE=7`, `TABLE_QUANTITY=8`, `TABLE_GARMENT=9`, `TABLE_SIZE=10`, `TABLE_SERVICE=11`, `TABLE_OBSERV=12`, `TABLE_EDIT_LOCK=13`

## Notes

- All DB updates identify the target row by `n_recibo` + `hash` pair — safe under sort/filter.
- Accounting-locked rows (`edit_lock=1`) cannot be modified; `updateDb()` checks this.
- The print button opens an `Imprimir` dialog with `isRecibo=true`.
- `PAY_ALL` and `PKU_ALL` buttons mark all visible rows paid or picked up in one operation.
- Total price display (`le_total_price`) sums from the proxy model rows, not the raw SQL result, so it always reflects the filtered set.
