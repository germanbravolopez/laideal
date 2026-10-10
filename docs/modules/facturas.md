# Facturas (`src/facturas/`)

Formal supplier invoice entry form (Herramientas → Formulario facturas). Distinct from customer receipts (`ingresos`). Writes to the `gastos` table.

A `QDialog` built in code with the shared [UiKit](uikit.md) style (no `.ui` file), `WA_DeleteOnClose`: explanation panel, a **Factura** group (Nº factura, Fecha, Empresa, Servicio, Descripción), an **Importe** group (Tipo de IVA, Importe total IVA incl., read-only Base imponible and Cuota de IVA), **Limpiar formulario** / **Guardar factura**, the result panel and Cerrar. Every outcome (saved, incomplete, unknown supplier, closed quarter, failed insert) is written in the result panel; there are no message boxes. Object names (`leFra`, `deFecha`, `cbEmpresa`, `cbServicio`, `leDescripcion`, `cbIva`, `leImporte`, `leBase`, `leIva`, `btnSave`, `btnReset`, `lblResult`) are used by the e2e bench.

## Source files

- `src/facturas/facturas.h/cpp`

## Key interface

```cpp
Facturas *ui = new Facturas(db, this);  // db injected via constructor
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

- `n_factura`, `servicio`, `empresa`, and `importe` must not be empty, and `importe` must be a number (decimal comma accepted).
- `empresa` must exist exactly in the `proveedores` table.
- Invoice date must fall in an open accounting quarter (`quarterIsClosed()`: a month without gastos inside a closed quarter is closed too).

## Save

Inserts a row into `gastos` with a sequential `id` (`readMaxValueInColumnFromTable() + 1`), through the dialog's own `db` connection, the `importe` stored in cents (`moneyText`). On success the result panel names the invoice, supplier, amount and rate, and the form is cleared for the next one; a failed insert is reported in red.
