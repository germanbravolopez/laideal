# Imprimir (`src/imprimir/`)

Orchestrates printing of receipts (recibos) and simplified invoices (facturas
simplificadas). Reads the ticket rows from `ingresos`, assembles a `TicketData`,
and renders it to an **ESC/POS** byte stream via the [`printing`](printing.md)
library, then sends those bytes **RAW** to the thermal printer queue.

> Since 9.x this no longer generates an Excel file or drives Excel COM. The old
> QXlsx + `.xlsx` + VBScript + `cscript` path was replaced by direct ESC/POS to
> the Epson TM-T20III — see the research dossier in
> [`printer/`](printer/README.md) and the runtime lib in [`printing.md`](printing.md).

## Source files

- `src/imprimir/imprimir.h/cpp`

## Key interface

```cpp
Imprimir *ui = new Imprimir(db, this);  // db injected via constructor
ui->isRecibo = true;            // receipt layout
ui->isCompleteInvoice = false;  // true adds billing address + DNI prompts
ui->verifactuIntegration = m_verifactuIntegration; // enables /GetQrCode fallback
ui->qrCode = result.qrCode;     // optional: pixmap from a fresh /Create response
ui->le_n_ticket->setText(ticketNumber);
ui->getTicketInfo();            // fetches matching rows from ingresos
ui->buildTicket(copyForClient, addPayedInfo);  // builds ESC/POS bytes (m_ticketBytes)
ui->printTicket();              // sends m_ticketBytes RAW to the printer queue
```

`buildTicket()` / `printTicket()` keep the historical two-step shape: callers
build a copy, optionally print it, then build + print the next copy. A second
`printTicket()` without an intervening `buildTicket()` re-sends the same bytes
(used by the RecogPrendas "second copy?" prompt).

## Layout modes

| `isRecibo` | `isCompleteInvoice` | Layout |
|------------|---------------------|--------|
| `true` | `false` | Standard customer receipt (recibo) |
| `false` | `false` | Simplified invoice (factura simplificada) |
| `false` | `true` | Full invoice with billing-address + DNI prompts |

## Process

1. Caller sets the flags and the ticket number, then `getTicketInfo()` loads all
   matching `ingresos` rows into `sqlQueryModel` (optionally scoped to a paid
   `verifactu_invoice_seq` for partial-payment events). Both queries carry
   `AND estado != 'Anulado'` so a locally voided garment never prints on a recibo
   or factura.
2. `buildTicket(copyForClient, addPayedInfo)` reads the model + `AppSettings` +
   the resolved QR into a `TicketData` and calls `TicketRenderer::render(data,
   paperDots())`. The result is cached in `m_ticketBytes`. `paperDots()` derives
   the printable width from `AppSettings::paperWidthMm()` (80 mm → 576 dots,
   58 mm → 420).
3. `printTicket()` hands `m_ticketBytes` to `ThermalPrinter::send(bytes,
   AppSettings::printerName())`. An empty printer name uses the Windows default
   printer. On failure it shows a `QMessageBox` (the old path had no such
   feedback). Callers still gate the call on `AppSettings::enablePrinting()`.
   When `AppSettings::useStatusApi()` is on, the bytes go through
   `StatusApiPrinter::sendAndReadStatus()` first (Epson Status API) and a device
   problem (paper out / cover open / cutter) is surfaced in a `QMessageBox`; if
   the Status API is unavailable it falls back to the RAW `send()` above.

The column layout is computed from the real paper width (dots / char width per
font) instead of empirically-tuned spreadsheet cell widths.

## Ticket layout

The renderer reproduces the historical content and ordering (see
[`printer/04_current_printing_flow.md`](printer/04_current_printing_flow.md)),
top to bottom:

| Block | Content |
|-------|---------|
| Header (centered) | `business.name` double-size; then `verifactu.company_name`, `NIF`, `business.address`, `business.city`, `Tlf:` (bold), closed by a `=` rule |
| Document type | `Recibo` or `FACTURA SIMPLIFICADA` |
| Identity | `Nº:` (bold, right), `Cliente:`, `Recepción:`, optional `Dirección:` / `DNI:` (complete invoice) |
| Garment table | `Uds. \| Prendas \| Importe`, one row per garment (paid rows only on a factura); long names word-wrap |
| Totals (bold) | recibo: `TOTAL (IVA incl.)`; factura: `Base Imponible` + `IVA (rate%)` + `TOTAL` |
| Payment / copy | `IMPORTE PAGADO` when `addPayedInfo`; recibo copy marker (`Copia para el cliente` / `... establecimiento`) |
| Verifactu QR | facturas only — rastered AEAT pixmap + the verification legend when estado = ENVIADA |
| Legal policy | client copy only — RD 1453/1987 clauses + RGPD notice (Font B) |
| Timestamp + cut | `dd/MM/yyyy - hh:mm:ss`, then feed + partial cut |

Header/identity fields are read from `AppSettings::instance()` at build time.

## General conditions block

Rendered only when `copyForClient == true` (the establishment copy omits it).
All clauses are grounded in RD 1453/1987 (BOE-A-1987-26716) and verified against
Consumo Responde (Junta de Andalucía):

| # | Topic | Key text | Legal basis |
|---|-------|----------|-------------|
| 1 | Receipt / identity | Receipt required to collect; loss does not prevent it — client identifies themselves | Custom clause, industry standard |
| 2 | Storage obligation | Storage fees may accrue after 3 months from delivery; custody obligation ends at 6 months | Art. 6 RD 1453/1987 (custody, not documentation) |
| 3 | Accessories | No liability for buttons/ornaments/accessories unless noted on *this receipt*; removal recommended | Art. 3 RD 1453/1987; disclaimer must be per-garment |
| 4 | Pre-cleaning advisory | If garment state implies risk of damage or uncertain outcome, client will be informed before treatment | Art. 6 RD 1453/1987 (information obligation) |
| 5 | RGPD notice | Data processed by `businessName()` to manage the service; rights under Arts.15-22 RGPD via `businessPhone()` (falls back to "ver cartel en tienda") | LOPDGDD Art. 11 / RGPD Art. 13 layered-notice |

## `ingresos` column indices

Defined once in `sql_lite/ingresos_schema.h` as `INGRESOS_COL_<UPPER_DB_COLUMN_NAME>`
(`INGRESOS_COL_N_RECIBO=0` … `INGRESOS_COL_VERIFACTU_INVOICE_ID=25`). Used here
for every `sqlQueryModel->index(row, col)` lookup in `imprimir.cpp`, and shared
with `recog_prendas`, `listado`, `mainwindow`, `mysortfilterproxymodel` and
`add_garment`.

## Verifactu QR

A QR is printed on **facturas only** (recibos are claim tickets, not tax
documents). `resolveQrCode()` is unchanged by the ESC/POS migration — order of
fallback:

1. If the caller pre-populated `qrCode` (the pixmap from a fresh `/Create`
   response in the save flow), use it.
2. Otherwise aggregate `verifactu_estado` across all rows: emit a QR only when at
   least one row has a non-empty `verifactu_csv` AND no row is `ANULADA` /
   `RECTIFICADA` / `ERROR`. The InvoiceID is `displayInvoiceId()` and the
   `FechaExpedicion` is the first paid row's `fecha_pago` (the date submitted to
   AEAT).
3. When eligible and `verifactuIntegration` is configured, fetch the QR
   asynchronously via REST `/GetQrCode` with a 5 s bounded wait; on timeout the
   ticket prints without a QR.

`buildTicket()` rasters the resolved `QPixmap` into the ESC/POS stream
(`qr.scaled(192,192).toImage()` → `EscPosBuilder::rasterImage()`, `GS v 0`), so
the printed QR is byte-exact with what AEAT issued (no local re-encoding). The
verification legend `Factura verificable en la sede electrónica de la AEAT` is
emitted below the QR only when row 0's `verifactu_estado` equals
`verifactuEstadoToString(VerifactuEstado::Enviada)` — required by Disposición
Final Primera RD 1007/2023. QR image bytes are never persisted in the DB.

## Dependencies

- [`printing`](printing.md) — `EscPosBuilder`, `TicketRenderer`, `ThermalPrinter`
  (the ESC/POS byte builder + RAW Windows-spooler transport). No QXlsx, no Excel,
  no `cscript`.
- `verifactu` — `VerifactuIntegration::generateQRAsync()` for the `/GetQrCode` path.
- `appsettings` — business identity, IVA rate, `printerName()`, `paperWidthMm()`,
  `enablePrinting()`.
- `sql_lite` — `searchItemFromClient()`, the `verifactuInvoiceId()`/estado helpers,
  and the `INGRESOS_COL_*` indices.
