# 04 — Current Printing Flow (what we are replacing)

This is the baseline: how `src/imprimir/` prints today, the exact layout the new ESC/POS code
must reproduce, and every place that calls the printing API (the integration surface a rewrite
must keep working). Source: [`../../src/imprimir/imprimir.cpp`](../../src/imprimir/imprimir.cpp)
and [`../../src/imprimir/imprimir.h`](../../src/imprimir/imprimir.h).

## The pipeline today

```
getTicketInfo()          → SELECT * FROM ingresos WHERE n_recibo = ?   (optionally AND seq, pagado)
createTicketExcel(...)   → QXlsx::Document writes ~/.laideal_ticket.xlsx (header, table, totals, QR, policy)
printTicket()            → writes ~/.laideal_print.vbs, runs:  cscript //nologo //B <vbs>
                              the VBS drives Excel COM: CreateObject("Excel.Application")
                              → Workbooks.Open(xlsx) → PrintOut → Close → Quit
                              → Excel prints to the DEFAULT Windows printer (the TM-T20III queue)
```

### What each piece costs us

- **Microsoft Excel must be installed and licensed** on the shop PC — a hard runtime dependency
  for printing a receipt.
- **COM automation** of Excel is slow (the process launches per print) and brittle (a left-over
  `EXCEL.EXE`, a dialog, a macro-security prompt can hang it; `process.waitForFinished(60000)`
  is a 60 s ceiling).
- A **`.vbs` script is generated to disk** and executed via `cscript` on every print — extra
  moving parts and an antivirus/Smart-screen surface.
- **QXlsx** (vendored, with a local `setPageMargins` patch) is pulled in only for this.
- **No status feedback** — the app cannot tell whether the ticket actually printed, or whether
  paper/cover was the problem.
- **Layout is fought through spreadsheet cells** — column widths in Excel units, merged cells,
  manual row heights — tuned empirically against the physical printer.

All of this disappears with direct ESC/POS.

## Exact layout to reproduce

`createTicketExcel(bool copyForClient, bool addPayedInfo)` builds three columns
(`Uds. | Prendas | Importe`) and these blocks, top to bottom. The new renderer must produce the
same content and ordering within the printer's column width.

1. **Header** (centered):
   - Business name — large (Excel 22 pt) → ESC/POS double-size, centered.
   - `verifactuName()` (company legal name), `NIF: <verifactuNif()>`, `businessAddress()`,
     `businessCity()`, `Tlf: <businessPhone()>` — bold; phone row closed by a double rule.
2. **Document type**: `Recibo` (claim ticket) **or** `FACTURA SIMPLIFICADA`.
3. **`Nº: <displayInvoiceId()>`** — bold, right-aligned. `displayInvoiceId()` = first non-empty
   `verifactu_invoice_id` across the rows, else the bare `n_recibo`.
4. **`Cliente: <name>`** (from row 0).
5. **`Recepción: <fecha_recepcion>`** (`-`→`/`).
6. **Complete-invoice extras** (only when `isCompleteInvoice`): `Dirección:` (looked up, or
   prompted; split across two lines if ≥ 42 chars) and `DNI:` (prompted).
7. **Double-rule separator**, then table header `Uds. | Prendas | Importe`.
8. **Garment rows** — for each `ingresos` row (recibo: all; factura: only paid rows):
   - `cantidad` | garment name (+ ` - <size>` when size ≠ 0) | `importe` (2 decimals).
   - Accumulates `ticketTotalF`.
9. **Totals**:
   - Recibo: `TOTAL (IVA incl.): <total>`.
   - Factura: `Base Imponible: <base>`, `IVA (<rate>%): <iva>`, `TOTAL: <total>` —
     `base = total / (1 + rate/100)`, `iva = total - base` (matches
     `Facturas::taxBaseFromGross`).
10. **`IMPORTE PAGADO`** line when `addPayedInfo`, else a blank spacer.
11. **Copy marker** (recibo only): `(Copia para el cliente)` or `(Copia para el establecimiento)`.
12. **Verifactu QR** (facturas only, never recibos): the `resolveQrCode()` `QPixmap` (≈140×140),
    followed — only when row 0 `verifactu_estado == ENVIADA` — by the small legend
    `Factura verificable en la sede electrónica de la AEAT`.
13. **Legal policy** (client copy only): the RD 1453/1987 clauses + RGPD first-layer notice
    (small font, fixed wording in `imprimir.cpp`).
14. **Timestamp**: `dd/MM/yyyy - hh:mm:ss`, top-ruled.

### QR / Verifactu rules to preserve (`resolveQrCode()`)

- Only on **facturas**, never recibos (a recibo is a claim ticket, not a tax document).
- Emit a QR only when at least one row has a real `verifactu_csv` **and** no row is
  `ANULADA` / `RECTIFICADA` / `ERROR`.
- Uses `displayInvoiceId()` for the InvoiceID and the first paid row's `fecha_pago` for the
  date (must equal what was submitted to AEAT).
- Fetches the QR **asynchronously** with a 5 s local-event-loop wait; prints without QR on
  timeout. The new code must keep this async-fetch-then-render behaviour (the QR pixmap is the
  byte-exact AEAT artifact — see the rastering note in
  [`03_escpos_command_reference.md`](03_escpos_command_reference.md)).

## Integration surface (callers + settings)

A rewrite should preserve the `Imprimir` public interface (or provide a drop-in) so these call
sites keep working. Today they all follow the pattern
`getTicketInfo()` → `createTicketExcel(copyForClient, addPayedInfo)` →
`if (enablePrinting()) printTicket()`.

| Caller | Location | Flow |
|--------|----------|------|
| Save ticket | [`mainwindow.cpp:548`](../../src/app/mainwindow.cpp#L548) | recibo: client copy then establishment copy. |
| Save invoice | [`mainwindow.cpp:565`](../../src/app/mainwindow.cpp#L565) | factura: two copies. |
| Print menu actions | [`mainwindow.cpp:803`](../../src/app/mainwindow.cpp#L803) | "Imprimir recibo / factura / factura completa" set `isRecibo`/`isCompleteInvoice` then exec the dialog. |
| Pay (Cobrar) | [`pay_dialog.cpp:393`](../../src/recog_prendas/pay_dialog.cpp#L393) | single customer factura after partial payment. |
| Pickup (Pagar todo / reprint) | [`recog_prendas.cpp:666`](../../src/recog_prendas/recog_prendas.cpp#L666) | factura on pickup, with a "second copy?" prompt. |
| Dialog OK (reprint) | [`imprimir.cpp:578`](../../src/imprimir/imprimir.cpp#L578) | `on_bb_ok_cancel_accepted()` — multi-seq factura chooser, recibo establishment-copy prompt. |

Public members used by callers: ctor `Imprimir(db, parent)`; `getTicketInfo()`;
`createTicketExcel(bool, bool)`; `printTicket()`; flags `isRecibo`, `isCompleteInvoice`,
`invoiceSeq`; `verifactuIntegration`; `qrCode`.

### Settings consumed (all already exist)

| Setting | Getter | Use |
|---------|--------|-----|
| `print.enable` | `enablePrinting()` | Guards the actual print (testing mode still built the file). |
| `business.name` | `businessName()` | Header title. |
| `business.address` / `business.city` / `business.phone` | `businessAddress()` / `businessCity()` / `businessPhone()` | Header lines; phone also RGPD contact. |
| `verifactu.company_name` / `verifactu.nif` | `verifactuName()` / `verifactuNif()` | Legal name + NIF in header. |
| `taxes.iva_rate` | `ivaRate()` (default 21) | Base/IVA split on facturas. |
| (internal) | `ticketExcelPath()` / `ticketPrintScriptPath()` | `~/.laideal_ticket.xlsx`, `~/.laideal_print.vbs` — **both retired** by the rewrite. |

> New settings the rewrite will add: printer queue name (and/or "use default printer"), paper
> width (58/80 mm). See [`05_implementation_plan.md`](05_implementation_plan.md).

## Things to delete when the rewrite lands

- `createTicketExcel()` / `printTicket()` bodies (replace with ESC/POS build + RAW send).
- `~/.laideal_ticket.xlsx` and `~/.laideal_print.vbs` generation + the `cscript` `QProcess`.
- The `QXlsx` dependency from `src/imprimir/CMakeLists.txt` (and, if no other module uses it,
  the vendored `QXlsx/` and its CMake wiring — verify first).
- The `AppSettings::ticketExcelPath()` / `ticketPrintScriptPath()` helpers.

## References

- [`../../src/imprimir/imprimir.cpp`](../../src/imprimir/imprimir.cpp),
  [`../../src/imprimir/imprimir.h`](../../src/imprimir/imprimir.h).
- [`../modules`](../modules) — per-module reference docs (MainWindow, recog_prendas).
- `progress_tracker.md` completed milestones on ticket column tuning and the QXlsx page-margin
  patch (the empirical 58 mm layout history).
