# Facturas (`src/facturas/`)

Supplier invoice entry (Contabilidad → Añadir factura de gastos). Not a customer receipt (`ingresos`): it writes one row to `gastos` per save. Source: `src/facturas/facturas.h/cpp`. Tests: `tests/test_facturas.cpp` (`TestFacturas`: `parseGross`, validation and duplicates, save), see [unit suites](../testing/unit_suites.md). Tables: [architecture](../architecture.md).

A `QDialog` built in code with [UiKit](uikit.md) (`WA_DeleteOnClose`): explanation, a **Factura** group (Nº factura, Fecha, Empresa, Servicio, Descripción), an **Importe** group (Tipo de IVA, Importe total IVA incl., read-only Base imponible and Cuota de IVA), **Limpiar formulario** / **Guardar factura**, the result panel, Cerrar. Every outcome is written in the result panel (no message boxes). Object names (`leFra`, `deFecha`, `cbEmpresa`, `cbServicio`, `leDescripcion`, `cbIva`, `leImporte`, `leBase`, `leIva`, `btnSave`, `btnReset`, `lblResult`) serve the e2e bench.

```cpp
Facturas *ui = new Facturas(db, this);   // fills both combos
ui->show();
// populateEmpresas() / populateServicios() clear and refill them after the lists change
```

## Fields

| Field | Source | Notes |
|-------|--------|-------|
| `n_factura` | free text | |
| `fecha` | date picker | defaults to today |
| `empresa` | combo from `proveedores`, Spanish alphabetical order (`UiKit::sortedNames`) | |
| `servicio` | editable combo from `servicios`, same order | any non-blank text, stored trimmed |
| `descripcion` | free text | optional |
| `iva` | combo 21 / 10 / 0 | the rates the accounting sums (`expenseIvaIsSummarised`, `sql_lite.h`) |
| `importe` | free text | total, IVA included |

Base and cuota update as `importe` or the rate change, with `splitGross` (`src/verifactu/verifactutypes.h`): base in cents, IVA the rest, so they add up to the amount typed.

## Validation

- `n_factura`, `servicio`, `empresa` and `importe` are required (trimmed).
- `importe` must be a plain positive amount (`parseGross`: comma or point decimals; empty text, signs, exponents, `inf`, `nan`, zero and anything over 999999999 refused).
- `empresa` must exist exactly (case included) in `proveedores`.
- The same supplier and Nº factura cannot be entered twice: the panel shows the existing invoice's date and amount.
- The invoice year must be within one of today's (a mistyped 2062 would store a row no report shows).
- The date must fall in an open quarter (`quarterIsClosed`: a month without gastos inside a closed quarter is closed too).

## Save

`insertExpenseInvoice` (`sql_lite`) inserts the row on the dialog's own `db` connection (left open if the caller had it open): `id` taken in the same statement (`MAX(id) + 1`), `importe` stored in cents (`moneyText`), and the statement itself refuses a date in a closed quarter, so a quarter closed meanwhile by an open Contabilidad window cannot receive it. On success the panel names invoice, supplier, amount and rate and the form clears; a failed insert is reported in red.
