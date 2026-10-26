# AddGarment (`src/add_garment/`)

Adds new garment rows to an existing ticket already in the database. Used when a client brings additional items after the initial ticket was created.

A `QDialog` built in code with the shared [UiKit](uikit.md) style (no `.ui` file), `WA_DeleteOnClose`: explanation panel; a search row (Nº recibo + **Buscar**, Enter also searches); a **Recibo** group (client and reception date, read-only, from the search); a **Prenda** group (Prenda, Servicio, Cantidad - integers only -, Tamaño m2, read-only Importe, Observaciones); an **Estado** group (**Pagada** / **Recogida** checkboxes, each enabling its date; they replaced the red/green toggle buttons); **Limpiar formulario** / **Añadir prenda**; the result panel; Cerrar. The three groups stay disabled until a search succeeds. Every outcome is written in the result panel (no message boxes) and the window stays open after a save, cleared for the next receipt. Object names (`leNRecibo`, `btnSearch`, `cbPrenda`, `leCantidad`, `chkPagado`, `btnSave`, `lblResult`, ...) are used by the e2e bench.

## Source files

- `src/add_garment/add_garment.h/cpp`

## Key interface

```cpp
AddGarment *ui = new AddGarment(db, this);  // db injected via constructor
ui->invoicingBlocked = [integration] { return integration->invoicingBlockedReason(); };  // required for paid adds
ui->aeatEnvironment  = [integration] { return integration->environmentName(); };         // optional, see below
connect(ui, &AddGarment::paidGarmentSaved, this, /* submit to AEAT */);
ui->show();
```

`invoicingBlocked` must be set by the creator (MainWindow and Recogida do): it returns why a paid garment cannot be invoiced now (empty = fine). **Unset, every paid add is refused** ("Verifactu no está disponible") - the fail-safe default. `aeatEnvironment` names the direct client's environment so the invoice number skips numbers already used there; unset, all environments count. `ticketFound()` is a read-only accessor (tests).

`paidGarmentSaved(ticketNum, paymentDate, amount, seq)` is emitted after a garment saved as **paid** is inserted; whoever opened the window submits it to AEAT at once - MainWindow (Tickets → Añadir nuevas prendas) with `verifactuSubmitInvoice(ticketNum, paymentDate, amount, seq)`, Recogida de prendas (its **Añadir prendas…** button, which opens the window on the selected ticket with `loadTicket`) with `retryVerifactuSubmit(ticketNum, seq)`. `garmentSaved(ticketNum)` is emitted for every saved garment (Recogida counts them).

## Workflow

1. User enters a receipt number and presses **Buscar** (`onSearchClicked`). If the ticket already has a **paid** garment (`sql_lite::ticketHasPaidGarment`), the search is refused in the result panel: a paid ticket has been submitted to AEAT, so only unpaid (not-yet-submitted) receipts may have garments appended locally.
2. If the ticket exists, the search fills client and reception date and `populateGarments()` fills the garment combobox from `prendas`; the form groups are enabled.
3. User selects garment, quantity, service, optional size, and optional payment info.
4. On **Añadir prenda**: `validationError()` runs the checks, then `saveGarment()` inserts a new row into `ingresos` with a fresh `genHash16()` hash. The insert goes through the shared `sql_lite::insertGarmentRow` seam with `verifactu_estado` `SIN COBRAR` when unpaid or `PENDIENTE` when paid (like `MainWindow::saveTicket`; issue #41). A garment saved as paid is an invoice: since step 1 refuses any ticket that already has a paid garment, it is the ticket's **first** payment event, dated with the payment date. Its `verifactu_invoice_seq` is `nextVerifactuInvoiceSeq(db, ticket, environment)`: 0 (InvoiceID = the ticket number) unless the direct client already used that number for an invoice that was never stored as paid, in which case the next free one. `paidGarmentSaved` carries the seq and has MainWindow submit it to AEAT immediately, like a paid ticket on save. The reply patches the row through MainWindow's usual handler; without one it stays `PENDIENTE` for the startup recovery. (Until October 2026 nothing submitted it until the next startup's recovery dialog.)

## Validation

`validationError()` runs these in order and shows the first failure in the result panel:

1. A search succeeded (`ticketFound()`), for the number actually saved: if the receipt number is retyped after the search, the save is refused ("No se ha buscado ningún Nº recibo…").
2. The paid-garment check is repeated, so a ticket charged meanwhile is refused too.
3. Client, garment and quantity must not be empty ("Formulario incompleto").
4. The garment must be an exact entry of the garment list: a free-typed name is refused, and it never triggers a price lookup or a pop-up while typing.
5. The quantity must be at least 1.
6. Garments whose name contains "m2" require a size greater than 0.
7. The importe must be more than 0,00 (`garmentImporte` clamps a negative size to 0).
8. If marking as paid: `invoicingBlocked()` must return empty (unset: refused).
9. The ticket's reception date must not be in a closed quarter (`quarterIsClosed`, the whole quarter), and neither must the payment date when paid.

The payment and pickup dates are bounded: never after today, never before the ticket's reception date. The payment date is the invoice date AEAT receives.

## Price calculation

Price is computed reactively on garment, service, quantity, or size change, and only for a garment that is an exact entry of the list (otherwise the importe shows 0,00 with no lookup). The maths is `sql_lite::garmentImporte(quantity, size, unitPrice)` (unit-tested), shared with MainWindow and Recogida:

```
price    = quantity * roundToCents(readGarmentPrice(db, garment, service))   // 0 if negative
importe  = roundToCents(size != 0 ? size * price : price), clamped to >= 0
```

Quantity and size accept a comma decimal (e.g. `2,6` m²); `garmentImporte` normalises them with `.trimmed().replace(",", ".")` and parses as `double`, since `QString::toFloat()` is C-locale only and would parse `2,6` as `0.0`. The size is stored with a dot at save time.

## Notes

- The receipt search uses a parameterised `QSqlQuery` — no SQL injection risk.
- The garment list is populated fresh each time a valid ticket is found (not at startup).
