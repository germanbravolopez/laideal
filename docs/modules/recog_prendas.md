# RecogPrendas (`src/recog_prendas/`)

"Recogida de prendas": search tickets, inspect or edit one garment, and act on its ticket (Cobrar, Recoger todo, Separar, Añadir / Anular prendas, Imprimir factura, Verifactu). Schema: [architecture.md](../architecture.md); `ingresos` column indices are `INGRESOS_COL_*` in `src/sql_lite/ingresos_schema.h`.

## Source files

- `recog_prendas.h/cpp` - the window (`QMainWindow`)
- `pay_dialog.h/cpp` - `PayDialog` (Cobrar)
- `voidgarmentsdialog.h/cpp` - `VoidGarmentsDialog` (Anular prendas)
- `invoicingpolicy.cpp` - `invoicingPolicyOf(VerifactuIntegration *)`, the `AddGarment::InvoicingPolicy` both wiring sites use
- `aeatreconcilesummary.h` / `.cpp` - `aeatReconcileSummaryHtml`, the pure text above the "Consultar en AEAT" table

## Window

Built in code with the shared [UiKit](uikit.md) style. The widgets are in the `Widgets` struct behind the `ui` pointer and keep their object names (`le_search`, `tableView`, `pb_pay_all`, `pb_state`, ...): the `on_<name>_<signal>` slots connect by name and the e2e bench finds them.

- **Búsqueda**: search box, *Fecha de* selector, Buscar, Limpiar.
- **Results table**: all columns except 0-12 and 26 (`fecha_anulacion`, "Anulación") are hidden.
- **Prenda seleccionada**: ticket, client and phones (read-only), garment, service, quantity, size, importe, notes.
- **Estado y fechas**: reception date, read-only **Pagada** with payment date and SÍ / NO label, **Recogida** with pickup date and Recogido / En tienda label, read-only **Anulación** date. An empty payment or anulación date shows "-"; an empty pickup date shows today (what Recogida will store). A voided garment shows no NO / En tienda label and no pickup date.
- **Ticket**: bold Importe total, Cobrar..., Recoger todo, Separar (+ how many), Añadir prendas..., Anular prendas..., Imprimir factura, Verifactu....
- A result panel: every message goes there. AEAT replies, which arrive later, use the status bar and the refresh they trigger keeps the panel's last message.
- The details follow the **current row**, so the arrow keys work without a click (`currentRowChanged`; the row already loaded keeps what the operator typed).

```cpp
RecogPrendas *ui = new RecogPrendas(db, this);
ui->m_verifactuIntegration = integration;   // required: without it Cobrar and retries refuse
ui->show();
```

`showTicket(nRecibo, seq = -1)` searches a ticket and selects the row of payment `seq` (used by the refused-invoices list).

## Search

`runSearch` classifies the text (all reads fully drain the model, `loadEveryRow`):

| Input | Query |
|-------|-------|
| Digits only (spaces ignored), under 9 | `n_recibo = :n` |
| Digits only, 9 or more (`+34` / `0034` prefix ignored) | `clientes` by `tel_fijo` / `movil`, then `ingresos` of every client sharing the number |
| A date (`dd/MM/yyyy` or `dd-MM-yyyy`) | The column picked by *Fecha de* (by position): Recepción, Pago, Recogida, Anulación (`fecha_anulacion`: voided, cancelled and rectified rows, total 0) |
| Any other text | All rows, filtered by `MySortFilterProxyModel::setSearchText` on the client: accents and case ignored, every word must appear |

Refreshes re-run the submitted query (`m_submittedQuery`) and reselect the garment by hash. Header clicks sort (`lessThan` handles dates and amounts); a new search sorts by `n_recibo` descending. The Importe total sums the proxy rows, skipping `garmentExcludedFromTotals` rows; unpaid rows count.

## Editing (`updateDb(UpdateDBop)`)

Each write delegates to a [sql_lite](sql_lite.md) function keyed by `(n_recibo, hash)`; the slot keeps the rules (closed quarter via `rowClickedLocked`, the `Anulado` lock).

| Operation | `UpdateDBop` | Writes |
|-----------|--------------|--------|
| Picked up | `PKU_YES` / `PKU_NO` | `updateTicketPickup` |
| Observations | `OBSV` | `updateTicketObservations` |
| Size | `SIZE_AND_PRICE` | `updateTicketSizeAndPrice` |
| Quantity | `QTY` | `updateGarmentQtyAndImporte` (re-priced with `garmentImporte`) |
| Service | `SERVICE` | `updateGarmentServiceAndImporte` (re-priced) |
| Importe typed by hand | `PRICE` | importe only (`manualUnitPrice`) |
| Separar | `SEPARATE_GARM` | `splitGarmentRow` |
| Recoger todo | not an op | `markTicketPickedUp` (this ticket only, `Anulado` rows excluded) |

Rules:
- Quantity (integers 1-9999), importe, size and service are editable only **before payment** (`!isAnulado && !isPaid && !editLock`); the sql_lite writers refuse a paid or sent row too. An unmeasured "m2" garment stays 0,00.
- A voided garment (`Anulado`) is read-only: state, pay, pickup-all and split are disabled and `updateDb` returns early. **Observations stay editable** so the reason can be recorded.
- `de_date_pickup` is the only date edit that writes. Payment, reception and anulación dates are display-only: `fecha_pago` is part of the AEAT invoice identity, so only `PayDialog` sets it.
- `pb_payment` is a disabled indicator; every payment goes through `PayDialog`.

## Buttons

All start disabled; `updateRowClickedToFields()` enables them for the selected row (`selectSourceRow` stores a source-model row).

| Button | Enabled when |
|--------|--------------|
| `pb_state`, `pb_pay_all`, `pb_pku_all`, `pb_separ_garm` | Row not `Anulado`; `sb_separ` (1 .. quantity - 1) only when quantity is over 1 |
| `pb_print` | Row selected; refuses an unpaid row. With none selected it prints the ticket's only payment event, or asks to select one |
| `pb_void`, `pb_add` | Row selected, any state (the dialogs refuse what cannot be done) |
| `pb_verifactu` | Row has a non-empty `verifactu_estado` |

- **Añadir prendas...** opens [AddGarment](add_garment.md) on the ticket (`loadTicket`, window-modal); a garment added as paid is sent with `retryVerifactuSubmit(ticket, seq)`; on close the window refreshes and reports how many were added.
- **Anular prendas...** opens `VoidGarmentsDialog` on the ticket (unpaid, never-sent rows only; `voidGarmentRow`) and reports how many were voided.

## Verifactu from this window

`pb_verifactu` opens a details dialog (estado, CSV, timestamp, error, link to the AEAT validation URL) with two buttons, both needing Verifactu configured:

- **Reintentar envío a AEAT** (estado `ERROR`): `retryVerifactuSubmit(ticket, seq)` re-submits that one payment event (`verifactuEventFor`: its own date and total, InvoiceID `n-seq`) asynchronously; `onVerifactuRequestFinished` applies the reply. A rectificativa and an invoicing block (`invoicingBlockedReason()`) are refused; every unsent request is explained in the result panel (`reportNotSent`).
- **Consultar en AEAT** (any paid row): `queryAeatAndOfferReconcile` asks AEAT, keeping the request in `m_pendingQueries` (request id -> ticket, seq, local event) until `onVerifactuQueryFinished`. `showAeatReconcileDialog` compares both records (text from `aeatReconcileSummaryHtml`, including "predates production") and offers to adopt the CSV (`adoptAeatRecord`) only when `verifactuRemoteMatches` holds and the record has a CSV. On a settled row the query is informational.

`m_pendingSubmits` maps request id -> payment event, including submits adopted from `PayDialog`, so a late reply still updates the rows (`updateTicketVerifactuFields`; 0 rows means already settled and is reported).

## PayDialog (Cobrar)

Lists the ticket's unpaid rows (voided ones skipped), each ticked; the operator unticks what is not paid now and clicks Cobrar. The payment date is limited from the reception date (or the production start, if later) to today. Cobrar refuses, in the result panel with nothing stored: an unmeasured m2 garment, a total of 0,00, no `m_verifactu` or an invoicing block, a payment date before production (`dateBeforeProductionReason`) or in a closed quarter, and an invoice sequence that cannot be read. Then:

1. `nextVerifactuInvoiceSeq(db, ticket, environment, &ok)` gives the seq; InvoiceID is `n-seq` (`verifactuInvoiceId`).
2. `storePayment` stores the rows first (`storeChargedGarments`: paid, PENDIENTE, seq, amounts in cents, all or none), so a crash leaves a payment the start-up recovery resends.
3. The invoice is submitted with `submitSimplifiedInvoiceAsync`. With the direct client the QR is known at once (`qrBeforeReply`): the factura prints before AEAT answers and RecogPrendas adopts the reply (`submitAdopted(..., facturaPrinted)`). Otherwise a 5 s bounded wait.
4. A definitive reply is written by `updateTicketVerifactuFields`. A timeout or network error leaves the rows PENDIENTE and prints a paid recibo without QR; the late reply is adopted.
5. Esc, the window X and Cerrar are refused while AEAT has not answered (`done`).

One customer copy is printed through `Imprimir` with `invoiceSeq` set, so only the paid subset shows. `pb_print` forwards the clicked row's `verifactu_invoice_seq` to `printFactura(ticket, seq)` so a reprint matches the AEAT invoice. `notSentReason()` says why a stored payment was not sent.

## Rules

- Rows whose `fecha_pago` is in a closed quarter cannot be modified (`rowClickedLocked`, `quarterIsClosed`).
- Updates identify the row by `n_recibo` + `hash`, safe under sort and filter.
- Business rules for pickup-all and splitting live in the `sql_lite` writers, so they are covered by `test_sql_lite`.

## Tests

`test_sql_lite` covers the writers; the windows are covered by the e2e bench and `tests/ui` ([testing](../testing/README.md)). `aeatReconcileSummaryHtml` is tested in `test_add_garment` ([unit_suites.md](../testing/unit_suites.md)).
