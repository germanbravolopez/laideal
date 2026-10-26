# Facturas (`src/facturas/`)

Formal supplier invoice entry form (Contabilidad → Añadir factura de gastos). Distinct from customer receipts (`ingresos`). Writes to the `gastos` table.

A `QDialog` built in code with the shared [UiKit](uikit.md) style (no `.ui` file), `WA_DeleteOnClose`: explanation panel, a **Factura** group (Nº factura, Fecha, Empresa, Servicio, Descripción), an **Importe** group (Tipo de IVA, Importe total IVA incl., read-only Base imponible and Cuota de IVA), **Limpiar formulario** / **Guardar factura**, the result panel and Cerrar. Every outcome (saved, incomplete, unknown supplier, closed quarter, failed insert) is written in the result panel; there are no message boxes. Object names (`leFra`, `deFecha`, `cbEmpresa`, `cbServicio`, `leDescripcion`, `cbIva`, `leImporte`, `leBase`, `leIva`, `btnSave`, `btnReset`, `lblResult`) are used by the e2e bench.

## Source files

- `src/facturas/facturas.h/cpp`

## Key interface

```cpp
Facturas *ui = new Facturas(db, this);  // db injected via constructor
ui->show();
// The constructor fills both combos (`proveedores`, `servicios`); populateEmpresas() /
// populateServicios() refill them (they clear first) after the lists changed.
```

## Form fields

| Field | Source | Notes |
|-------|--------|-------|
| n_factura | Free text | Invoice number |
| fecha | Date picker | Defaults to today |
| servicio | Editable combobox | Suggestions from the `servicios` table, in Spanish alphabetical order (`UiKit::sortedNames`); any non-blank text is accepted and stored trimmed |
| descripcion | Free text | Optional description |
| empresa | Combobox | From `proveedores` table, in Spanish alphabetical order (`UiKit::sortedNames`) |
| iva | Combobox | 21 / 10 / 0 |
| importe | Free text | Total amount; base and IVA auto-computed |

## Auto-calculation

On `importe` or IVA change, the base and IVA display fields update:

```
base = importe / (1 + iva/100)
iva_amount = importe - base
```

## Validation

- `n_factura`, `servicio`, `empresa`, and `importe` must not be empty (trimmed: a blank servicio is refused; servicio stays free text), and `importe` must be a plain positive amount (`Facturas::parseGross`: decimal comma accepted; zero, negative, `inf`, `nan`, exponents and signs refused).
- The same supplier and Nº factura cannot be entered twice: the panel shows the existing invoice's date and amount.
- `empresa` must exist exactly (case included) in the `proveedores` table.
- The invoice date must be within a year of today (a mistyped year such as 2062 would store a row no report shows).
- Invoice date must fall in an open accounting quarter (`quarterIsClosed()`: a month without gastos inside a closed quarter is closed too).

## Save

Inserts a row into `gastos` through `insertExpenseInvoice` (`sql_lite`): the `id` is taken in the same statement (`MAX(id) + 1`, no separate read and no pop-up), the `importe` is stored in cents (`moneyText`), and the statement itself refuses a date in a closed quarter (so a quarter closed meanwhile by an open Contabilidad window cannot receive the row), on the dialog's own `db` connection (left open if the caller had it open). On success the result panel names the invoice, supplier, amount and rate, and the form is cleared for the next one; a failed insert is reported in red.
