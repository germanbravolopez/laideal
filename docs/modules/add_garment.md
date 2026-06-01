# AddGarment (`src/add_garment/`)

Adds new garment rows to an existing ticket already in the database. Used when a client brings additional items after the initial ticket was created.

## Source files

- `src/add_garment/add_garment.h/cpp`

## Key interface

```cpp
AddGarment *ui = new AddGarment(db, this);  // db injected via constructor
ui->show();
```

## Workflow

1. User enters a receipt number and presses **Search** (`on_pb_search_pressed`).
2. If the ticket exists, `fillContentFromDb()` populates client and reception date; `populateGarments()` fills the garment combobox from `prendas`.
3. User selects garment, quantity, service, optional size, and optional payment info.
4. On **Save**: `validateForm()` runs checks, then `saveFactura()` inserts a new row into `ingresos` with a fresh `genHash16()` hash.

## Validation

- Search must succeed before saving (`ticketFound=true`).
- Receipt number, client, garment, and quantity must not be empty.
- Garments whose name contains "m2" require a non-empty size field.
- If marking as paid: date must fall in an unlocked accounting quarter.

## Price calculation

Price is computed reactively on garment, service, quantity, or size change:

```
price = quantity * readGarmentPrice(db, garment, service)
if size != 0:
    price *= size
```

Quantity and size are normalised with `.trimmed().replace(",", ".")` before `toFloat()` — Spanish input often uses a comma decimal (e.g. `2,6` m²), and `QString::toFloat()` is C-locale only, so without normalisation the value parses as `0.0` and the size factor is dropped (here it would zero the importe). This matches the save-time `replace(",",".")` used when binding `:importe` / `:size`. `MainWindow::setGarmentPrice` applies the same normalisation.

## Notes

- The receipt search uses a parameterised `QSqlQuery` — no SQL injection risk.
- The garment list is populated fresh each time a valid ticket is found (not at startup).
