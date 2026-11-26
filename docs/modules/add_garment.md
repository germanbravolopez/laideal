# AddGarment (`src/add_garment/`)

Adds one garment row to an existing receipt that has no paid garment yet (Tickets -> Añadir nuevas prendas, and Recogida's **Añadir prendas...** button).

Source: `src/add_garment/add_garment.h/cpp`. A `QDialog` built in code with the shared [UiKit](uikit.md) style (no `.ui`), `WA_DeleteOnClose`. Object names (`leNRecibo`, `btnSearch`, `cbPrenda`, `leCantidad`, `chkPagado`, `btnSave`, `lblResult`, ...) are used by the e2e bench.

## Form

- Search row: receipt number + **Buscar** (Enter also searches).
- Three groups, disabled until a search succeeds: **Recibo** (client and reception date, read-only), **Prenda** (garment, service, quantity, size m2, read-only importe, notes), **Estado** (**Pagada** / **Recogida** checkboxes, each enabling its date).
- Every outcome goes to the result panel (no message boxes); the window stays open after a save.
- After an unpaid save only the garment fields clear (the next garment goes to the same receipt). After a paid save the whole form resets: the ticket now has an invoice and would be refused.

## Interface

```cpp
AddGarment *ui = new AddGarment(db, this);
ui->invoicing = invoicingPolicyOf(integration);   // recog_prendas.h; null-safe
connect(ui, &AddGarment::paidGarmentSaved, this, /* submit to AEAT */);
```

| Member | Purpose |
|--------|---------|
| `loadTicket(num)` | Types the number and searches it (Recogida). |
| `ticketFound()` | Read-only accessor (tests). |
| `invoicing` | An `AddGarment::InvoicingPolicy`: the four callbacks below, built in one place by `invoicingPolicyOf(VerifactuIntegration *)` (`src/recog_prendas/invoicingpolicy.cpp`) for both the main window and Recogida. |
| `invoicing.invoicingBlocked` | Why a paid garment cannot be invoiced now, or empty. Unset: every paid add is refused. |
| `invoicing.invoiceDateRefused(date)` | Why an invoice cannot carry that payment date (before production), or empty. |
| `invoicing.productionStartDate` | Day production began: the payment date's minimum. |
| `invoicing.aeatEnvironment` | Direct client's environment, whose used numbers a new invoice skips. Unset: all count. |
| `garmentSaved(ticket)` | Emitted for every saved garment. |
| `paidGarmentSaved(ticket, paymentDate, amount, seq)` | Emitted for a garment saved as paid: the opener submits it to AEAT at once (MainWindow through `VerifactuCoordinator::submitInvoice`, Recogida with `retryVerifactuSubmit(ticket, seq)`). |

## Rules

1. **Search** (`onSearchClicked`): the ticket must exist (`sql_lite::ticketHeader`) and have no paid garment (`ticketHasPaidGarment`; an unreadable answer is refused too). A paid ticket is an invoice AEAT holds.
2. **Save** (`validationError`, first failure shown):
   1. the number is the one searched (retyping it after the search is refused);
   2. the paid check is repeated;
   3. client, garment and quantity are filled;
   4. the garment is an exact entry of the garment list;
   5. quantity at least 1;
   6. an "m2" garment has a size above 0;
   7. importe above 0,00;
   8. if paid: `invoicingBlocked()` is empty (unset: refused) and `invoiceDateRefused(paymentDate)` is empty;
   9. neither the reception date nor, if paid, the payment date is in a closed quarter (`quarterIsClosed`).
3. **Dates**: payment and pickup dates are never after today nor before the reception date. The **payment** date is also limited to `productionStartDate()` or later, since it is the invoice date AEAT receives; the limits are reset on every search.
4. **Insert** (`saveGarment`): a new row through `sql_lite::insertGarmentRow` with a fresh `genHash16()` hash and `verifactu_estado` `SIN COBRAR` (unpaid) or `PENDIENTE` (paid), like `MainWindow::saveTicket`. A paid garment is the ticket's first payment event, dated with the payment date; its `verifactu_invoice_seq` comes from `nextVerifactuInvoiceSeq(db, ticket, environment, &ok)` (0 unless the direct client already used that number). If the sequence cannot be read, nothing is stored.

## Price

Computed on garment, service, quantity or size change, only for an exact list entry (else 0,00): `sql_lite::garmentImporte(quantity, size, unitPrice)`, shared with MainWindow and Recogida. Quantity and size accept a comma decimal. The garment list is read from `prendas` each time a ticket is found.
