# Imprimir (`src/imprimir/`)

Prints receipts (recibos) and invoices (facturas simplificadas and completas). It reads the ticket's rows from `ingresos`, assembles a `TicketData`, renders it to an ESC/POS byte stream with the [`printing`](printing.md) library and sends the bytes RAW to the thermal printer queue. Source: `src/imprimir/imprimir.h/cpp`. Tests: `tests/test_imprimir.cpp` (`TestImprimir`, offscreen dialog and engine cases) and `tests/test_ticket_preview.cpp` (rendered sample), see [unit suites](../testing/unit_suites.md). Hardware research: [`printer/`](printer/README.md).

## Two roles

- **Dialog** (Imprimir → Recibo / Factura / Factura completa), built in code with [UiKit](uikit.md). Explanation panel, **Ticket** group (Nº recibo, Enter prints once through `UiKit::onEnter`; a **Factura** list when the ticket has several payment invoices; for a recibo, **Imprimir también la copia del establecimiento**, ticked), for a completa a **Datos del cliente** group (Dirección, DNI / NIF), the primary button, the result panel, Cerrar. The window stays open and every outcome is written in the result panel.
- **Print engine** (MainWindow save, Cobrar, Recogida): callers set the public fields and call `getTicketInfo` / `buildTicket` / `printTicket` without showing it. `printTicket()` returns whether the ticket reached the printer; problems pop up (`reportPrinterProblem`) since there is no panel.

```cpp
Imprimir *ui = new Imprimir(db, this);
ui->isRecibo = true;             // receipt layout
ui->isCompleteInvoice = false;   // true: billing address + DNI
ui->verifactuIntegration = m_verifactuIntegration;  // resolves the QR of a reprint
ui->qrCode = result.qrCode;      // optional: pixmap of a fresh /Create response
ui->verifactuIssued = true;      // optional: the direct client just issued the record
ui->invoiceSeq = seq;            // -1: every row of the ticket; >= 0: paid rows of that payment event
ui->le_n_ticket->setText(ticketNumber);
ui->getTicketInfo();             // loads the ingresos rows
ui->buildTicket(copyForClient, addPayedInfo);   // fills m_ticketBytes
ui->printTicket();               // sends them; a second call without a build re-sends the same bytes
ui->ticketBytes();               // the last built bytes (tests read them)
```

Engine callers: `PayDialog` prints the partial factura with `invoiceSeq` (and, when AEAT did not reply, a recibo with `IMPORTE PAGADO` instead); Recogida reprints with the clicked row's seq; MainWindow's save-time print passes the QR it already holds.

## Rules

| `isRecibo` | `isCompleteInvoice` | Layout |
|-----------|---------------------|--------|
| true | false | Recibo (claim ticket, no QR) |
| false | false | Factura simplificada |
| false | true | Factura completa: same `FACTURA SIMPLIFICADA` heading (its AEAT record is F2) plus the recipient's address and DNI / NIF |

- **Loading** (`getTicketInfo`): the ticket number is trimmed and both queries exclude `estado = 'Anulado'` rows (NULL estado still prints). With `invoiceSeq >= 0` the query also forces `verifactu_invoice_seq = :seq AND pagado = 'SI'` (the seq column defaults to 0, so a seq-0 event would otherwise pull in unpaid siblings). A factura prints only rows whose `pagado` is exactly `SI`. `buildTicket` builds nothing when no row is printable, and the dialog says so instead of "impreso".
- **Facturas are one payment event, never merged.** The dialog lists the `(verifactu_invoice_seq, invoice id)` pairs that have a `verifactu_estado`. With one, it scopes to that seq. With several, the first press fills the **Factura** list (one entry per invoice plus *Todas las facturas*) and the second prints the chosen one or each, naming in the panel the ones printed and the ones not (no paid garments, missing address, printer error). An invoice cancelled or rectified since prints marked `ANULADA` / `RECTIFICADA`, without QR.
- **Complete invoice**: refused until there is a DNI / NIF and a recipient address (typed, else the client's saved one: `recipientAddress()`); the panel says what is missing and nothing prints.
- **Recibo**: prints `IMPORTE PAGADO` only when every garment of the ticket is paid (`ticketAllGarmentsPaid`, voided garments ignored). Its Nº is the ticket number, never a payment event's invoice id.
- **Copies**: if the client copy fails the shop copy is not attempted. A failing shop copy is reported on its own ("la copia del cliente se ha impreso, la del establecimiento no").
- **Printing disabled** (`AppSettings::enablePrinting()` false, gated by the callers): the ticket is built but not sent and the panel says "preparado, no impreso".
- **Result panel** also states: not found, no paid garment, no QR (Verifactu not active, not confirmed by AEAT, or cancelled), and printer problems.
- **Printing** (`printTicket`): bytes go to `ThermalPrinter::send(bytes, AppSettings::printerName())` (empty name: default printer, see [printing](printing.md)). With `useStatusApi()` on they go through `StatusApiPrinter::sendAndReadStatusBounded()` first: a device problem is reported (panel or message box), a fatal fault stops there, an unavailable or timed-out API falls back to RAW, and a hang inside the send is not resent.
- `paperDots()` comes from `AppSettings::paperWidthMm()` (80 mm: 576 dots, 58 mm: 420); header and identity fields are read from `AppSettings` at build time.

## Ticket layout

| Block | Content |
|-------|---------|
| Header | `business.name` double-size, `verifactu.company_name`, NIF, address, city, `Tlf:`, `=` rule |
| Document type | `Recibo` or `FACTURA SIMPLIFICADA`. A cancelled or rectified invoice adds `*** FACTURA ANULADA ***` / `*** FACTURA RECTIFICADA ***` and "Factura anulada el <fecha>. Esta copia no es una factura en vigor."; no QR or leyenda |
| Identity | `Nº:`, `Fecha de expedición:` (facturas: the paid rows' `fecha_pago`, the date in the QR and at AEAT), `Cliente:`, `Recepción:`, `Dirección:` / `DNI:` (completa) |
| Garments | `Uds. \| Prendas \| Importe`, one row per garment (paid rows only on a factura), long names wrap |
| Totals | recibo `TOTAL (IVA incl.)`; factura `Base Imponible`, `IVA (rate%)`, `TOTAL` |
| Payment / copy | `IMPORTE PAGADO` when `addPayedInfo`; recibo copy marker |
| QR | facturas only, see below |
| Legal policy | client copy only: RD 1453/1987 clauses (receipt needed to collect, custody fees after 3 months and end of custody at 6, accessories, pre-cleaning risk) and the RGPD notice (`businessName()`, `businessPhone()`), Font B |
| Footer | `dd/MM/yyyy - hh:mm:ss`, feed, partial cut |

## Verifactu QR

Printed on facturas only. `resolveQrCode()` resolves it in this order:

1. The caller's `qrCode` (a fresh `/Create` response, or `PayDialog`'s `qrBeforeReply`).
2. No QR when Verifactu is not configured, when any loaded row is `ANULADA` / `RECTIFICADA` / `ERROR` / unrecognised, or when the paid rows belong to more than one payment event (`invoiceSeq` -1 over a partially paid ticket): a QR always describes one invoice AEAT received. The amount sums only the paid rows; InvoiceID is `displayInvoiceId()`, `FechaExpedicion` the first paid row's `fecha_pago`.
3. No row has a CSV (AEAT has not confirmed): only the direct client has a QR, the one printed at issue (`VerifactuIntegration::qrBeforeReply`). That sets `m_qrFromQueuedRecord`, which also turns the leyenda on.
4. A CSV exists: the QR is drawn (`AeatQr::image`) from the stored `verifactu_url_qr`, never rebuilt from today's environment or IVA rate. A row without that URL (an old gateway row) asks the gateway (`/GetQrCode`, 5 s wait; on timeout no QR); the direct client prints no QR rather than rebuild it.

`buildTicket()` redraws the QR through `TicketRenderer::printableQr` (30-40 mm, see [printing](printing.md)). A symbol it cannot read is printed as received, scaled down only when wider than the paper. The leyenda is printed only when every paid row has `verifactu_estado` `ENVIADA`, or `PENDIENTE` while the direct client has just issued the record (`verifactuIssued` or `m_qrFromQueuedRecord`); never decided by row 0. QR bytes are never stored. Legal basis: [Verifactu](verifactu/README.md).

## Public API

| Group | Members |
|-------|---------|
| Inputs | `isRecibo`, `isCompleteInvoice`, `verifactuIntegration`, `qrCode`, `verifactuIssued`, `invoiceSeq`, `le_n_ticket` |
| Flow | `getTicketInfo`, `buildTicket`, `printTicket`, `ticketBytes` |
| QR / ids | `resolveQrCode`, `displayInvoiceId` (the pure seam `sql_lite::verifactuDisplayInvoiceId`: first non-empty `verifactu_invoice_id`, else bare `n_recibo`) |

`ingresos` column indices are the `INGRESOS_COL_*` macros in `sql_lite/ingresos_schema.h`, used for every `sqlQueryModel->index(row, col)` lookup.

## Dependencies

[`printing`](printing.md) (`TicketRenderer`, `ThermalPrinter`, `StatusApiPrinter`), `verifactu` (`qrBeforeReply`, `generateQRAsync`, `AeatQr::image`), [`uikit`](uikit.md), `appsettings` (business identity, IVA rate, `printerName()`, `paperWidthMm()`, `enablePrinting()`, `useStatusApi()`), `sql_lite` (`ticketAllGarmentsPaid`, estado helpers, `INGRESOS_COL_*`).
