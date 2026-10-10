# AddGarment (`src/add_garment/`)

Adds new garment rows to an existing ticket already in the database. Used when a client brings additional items after the initial ticket was created.

A `QDialog` built in code with the shared [UiKit](uikit.md) style (no `.ui` file), `WA_DeleteOnClose`: explanation panel; a search row (Nº recibo + **Buscar**, Enter also searches); a **Recibo** group (client and reception date, read-only, from the search); a **Prenda** group (Prenda, Servicio, Cantidad - integers only -, Tamaño m2, read-only Importe, Observaciones); an **Estado** group (**Pagada** / **Recogida** checkboxes, each enabling its date; they replaced the red/green toggle buttons); **Limpiar formulario** / **Añadir prenda**; the result panel; Cerrar. The three groups stay disabled until a search succeeds. Every outcome is written in the result panel (no message boxes) and the window stays open after a save, cleared for the next receipt. Object names (`leNRecibo`, `btnSearch`, `cbPrenda`, `leCantidad`, `chkPagado`, `btnSave`, `lblResult`, ...) are used by the e2e bench.

## Source files

- `src/add_garment/add_garment.h/cpp`

## Key interface

```cpp
AddGarment *ui = new AddGarment(db, this);  // db injected via constructor
connect(ui, &AddGarment::paidGarmentSaved, this, /* submit to AEAT */);
ui->show();
```

`paidGarmentSaved(ticketNum, paymentDate, amount)` is emitted after a garment saved as **paid** is inserted; MainWindow submits it to AEAT at once (`verifactuSubmitInvoice(ticketNum, paymentDate, amount, 0)`).

## Workflow

1. User enters a receipt number and presses **Buscar** (`onSearchClicked`). If the ticket already has a **paid** garment (`sql_lite::ticketHasPaidGarment`), the search is refused in the result panel: a paid ticket has been submitted to AEAT, so only unpaid (not-yet-submitted) receipts may have garments appended locally.
2. If the ticket exists, the search fills client and reception date and `populateGarments()` fills the garment combobox from `prendas`; the form groups are enabled.
3. User selects garment, quantity, service, optional size, and optional payment info.
4. On **Añadir prenda**: `validationError()` runs the checks, then `saveGarment()` inserts a new row into `ingresos` with a fresh `genHash16()` hash. The insert goes through the shared `sql_lite::insertGarmentRow` seam with `verifactu_estado` `SIN COBRAR` when unpaid or `PENDIENTE` when paid (like `MainWindow::saveTicket`; issue #41). A garment saved as paid is an invoice: since step 1 refuses any ticket that already has a paid garment, it is always the ticket's **first** payment event - seq 0, InvoiceID = the ticket number, dated with the payment date - and `paidGarmentSaved` has MainWindow submit it to AEAT immediately, like a paid ticket on save. The reply patches the row through MainWindow's usual handler; without one it stays `PENDIENTE` for the startup recovery. (Until October 2026 nothing submitted it until the next startup's recovery dialog.)

## Validation

- Search must succeed before saving (`ticketFound=true`), for the number actually saved: if the receipt number is retyped after the search, the save is refused ("No se ha buscado ningún Nº recibo…"). The paid-garment check is repeated at save, so a ticket charged meanwhile is refused too.
- Garment and quantity must not be empty.
- Garments whose name contains "m2" require a size greater than 0.
- If marking as paid: the payment date must fall in an open quarter (`quarterIsClosed`, the whole quarter).

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
