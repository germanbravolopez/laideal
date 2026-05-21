# Imprimir (`src/imprimir/`)

Creates Excel files for printing receipts and formal invoices, then launches an external process to print them.

## Source files

- `src/imprimir/imprimir.h/cpp`

## Key interface

```cpp
Imprimir *ui = new Imprimir(this);
ui->db = db;
ui->isRecibo = true;            // receipt layout
ui->isCompleteInvoice = false;  // true adds extra fields (policy, stamp, etc.)
ui->verifactuIntegration = m_verifactuIntegration; // enables /GetQrCode fallback
ui->qrCode = result.qrCode;     // optional: pixmap from a fresh /Create response
ui->getTicketInfo();            // shows dialog for ticket number; fetches rows from DB
ui->createTicketExcel(copyForClient, addPayedInfo);
ui->printTicket();
```

## Layout modes

| `isRecibo` | `isCompleteInvoice` | Layout |
|------------|---------------------|--------|
| `true` | `false` | Standard customer receipt |
| `false` | `false` | Simple invoice |
| `false` | `true` | Full invoice with extra info dialog prompts |

## Process

1. User enters a ticket number in the dialog shown by `getTicketInfo()`.
2. All matching rows from `ingresos` are loaded into `sqlQueryModel`.
3. `createTicketExcel()` builds an `.xlsx` file via `QXlsx` with appropriate column widths, styles, and row content.
4. `printTicket()` launches an external `.bat` script that sends the Excel to the printer.

## Excel ticket layout

Rows are written sequentially using the local `row` counter:

| Rows | Content | Format |
|------|---------|--------|
| 1 | `business.name` | font 22 |
| 2 | `"     " + verifactu.company_name` | font 11, bold |
| 3 | `"     NIF: " + verifactu.nif` | font 11, bold |
| 4 | `"     " + business.address` | font 11, bold |
| 5 | `"     " + business.city` | font 11, bold |
| 6 | `"Tlf: " + business.phone` | font 11, bold, row height 14 |
| next | "Recibo" or "FACTURA SIMPLIFICADA" | — |
| … | ticket number, client, dates, optional address/DNI | — |
| … | double-line separator, garments table | — |
| … | totals block (receipt: TOTAL only; invoice: base + IVA + TOTAL) | bold |
| … | Verifactu QR image (140×140, if available) | — |
| … | payment info / copy label | — |
| … | general conditions (client copy only — see below) | font 9–10 |
| last | timestamp | font 9, top border |

All header fields are read from `AppSettings::instance()` at render time.

## General conditions block

The conditions block is rendered only when `copyForClient == true` (establishment copy omits it entirely).

All clauses are grounded in RD 1453/1987 (BOE-A-1987-26716) and verified against Consumo Responde (Junta de Andalucía):

| # | Topic | Key text | Legal basis |
|---|-------|----------|-------------|
| 1 | Receipt / identity | Receipt required to collect; loss does not prevent it — client identifies themselves | Custom clause, industry standard |
| 2 | Storage obligation | Storage fees may accrue after 3 months from delivery; custody obligation ends at 6 months | Art. 6 RD 1453/1987 (custody, not documentation) |
| 3 | Accessories | No liability for buttons/ornaments/accessories unless noted on *this receipt*; removal recommended | Art. 3 RD 1453/1987; disclaimer must be per-garment |
| 4 | Pre-cleaning advisory | If garment state implies risk of damage or uncertain outcome, client will be informed before treatment | Art. 6 RD 1453/1987 (information obligation) |
| 5 | RGPD notice | Data processed by `businessName()` to manage the service; rights under Arts.15-22 RGPD via `businessPhone()` (falls back to "ver cartel en tienda") | LOPDGDD Art. 11 / RGPD Art. 13 layered-notice |

## Table column indices (imprimir.h)

`TABLE_TICKET=0`, `TABLE_CLIENT=1`, `TABLE_DATE_RCP=2`, `TABLE_DATE_PAY=3`, `TABLE_DATE_PKU=4`, `TABLE_PRICE=5`, `TABLE_IS_PAYED=6`, `TABLE_STATE=7`, `TABLE_QUANTITY=8`, `TABLE_GARMENT=9`, `TABLE_SIZE=10`, `TABLE_SERVICE=11`, `TABLE_OBSERV=12`, `TABLE_EDIT_LOCK=13`, `TABLE_HASH=14`, `TABLE_VERIFACTU_CSV=15`, `TABLE_VERIFACTU_TS=16`, `TABLE_VERIFACTU_ESTADO=17`, `TABLE_VERIFACTU_ERROR=18`, `TABLE_VERIFACTU_URL_QR=19`

## Verifactu QR

The receipt embeds the Verifactu QR (140×140) at the bottom via `QXlsx::Document::insertImage()`. Order of fallback inside `resolveQrCode()`:

1. If the caller pre-populated `qrCode` (e.g., the pixmap from a fresh `/Create` response in the save flow), use it.
2. Otherwise, if `verifactuIntegration` is configured and the ticket row in `ingresos` has a non-empty `verifactu_csv`, call `VerifactuIntegration::generateQR(invoiceNumber, invoiceDate, taxBase, ivaRate)` → REST `/GetQrCode` and use the returned pixmap.
3. If neither yields a pixmap (no Verifactu, network error, etc.) the receipt is printed without a QR.

QR image bytes are **not** persisted in the DB; they are always reconstructed from the response or the REST endpoint.

## Dependencies

- `QXlsx` — third-party Excel r/w library in `QXlsx/` (do not modify)
- `verifactu` — `VerifactuIntegration::generateQR()` for the `/GetQrCode` fallback
- External `.bat` printing script — path set at deploy time
