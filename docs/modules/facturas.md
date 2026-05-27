# Facturas (`src/facturas/`)

Formal supplier invoice entry form. Distinct from customer receipts (`ingresos`). Writes to the `gastos` table.

## Source files

- `src/facturas/facturas.h/cpp`

## Key interface

```cpp
Facturas *ui = new Facturas(this);
ui->db = db;
ui->populateEmpresas();   // fill supplier combobox from `proveedores`
ui->populateServicios();  // fill service combobox from `servicios`
ui->show();
```

## Form fields

| Field | Source | Notes |
|-------|--------|-------|
| n_factura | Free text | Invoice number |
| fecha | Date picker | Defaults to today |
| servicio | Combobox | From `servicios` table |
| descripcion | Free text | Optional description |
| empresa | Combobox | From `proveedores` table |
| iva | Combobox | 21 / 10 / 0 |
| importe | Free text | Total amount; base and IVA auto-computed |

## Auto-calculation

On `importe` or IVA change, the base and IVA display fields update:

```
base = importe / (1 + iva/100)
iva_amount = importe - base
```

## Validation

- `n_factura`, `servicio`, `empresa`, and `importe` must not be empty.
- `empresa` must exist exactly in the `proveedores` table.
- Invoice date must fall in an unlocked accounting quarter (`readLockForMonthAndYear()`).

## Save

Inserts a row into `gastos` with a sequential `id` (`readMaxValueInColumnFromTable() + 1`).
