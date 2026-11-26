# printing (`src/printing/`)

Direct ESC/POS thermal printing for the Epson TM-T20III (and any ESC/POS printer). It builds the receipt as a byte stream and sends it RAW through the Windows spooler: no Excel, no COM, no Epson SDK (only `winspool`). Callers: [`imprimir`](imprimir.md). Background and design: [`printer/`](printer/README.md), with a rendered [sample recibo + factura](printer/README.md#sample-rendered-output).

Built as the static lib `printing` (links `Qt::Gui` for `QImage`, and `winspool` on Windows). `imprimir` links it; `settingsdialog` links it for `StatusApiPrinter::runDiagnosticsBounded`. Tests: `tests/test_escpos.cpp` (exact bytes, PC858 table, column math, rendered fragments, ASB decode), see [unit suites](../testing/unit_suites.md). `ThermalPrinter` and `StatusApiPrinter` are integration-only.

## Pieces

| File | Role |
|------|------|
| `escposbuilder.h/cpp` | Pure: accumulates ESC/POS bytes, PC858 transcode. No I/O |
| `ticketrenderer.h/cpp` | Pure: `TicketData` to receipt / invoice bytes; same input, same bytes |
| `thermalprinter.h/cpp` | Win32 RAW spooler transport (stub off Windows) |
| `printerqueue.h/cpp` | `PrinterQueue::defaultName()`: the one default-printer lookup (empty if none, always empty off Windows), used by both transports |
| `printerstatus.h/cpp` | Pure ASB status-word decoder (`PrinterStatus`) |
| `statusapiprinter.h/cpp` | Optional Epson Status API transport (`EPSStmApi.dll` loaded dynamically; stub off Windows) |

## EscPosBuilder

Chainable. It tracks the current font and paper width, so `columns()` is the usable character count (dots / char width: Font A 12 dots, Font B 9) and the padding helpers pad to it.

| Method | ESC/POS | Notes |
|--------|---------|-------|
| `init()` | `ESC @` + `ESC t 19` | reset, select PC858 |
| `align(a)`, `bold(on)`, `font(f)`, `doubleSize(on)` | `ESC a`, `ESC E`, `ESC M`, `GS !` | font A is 48 columns at 80 mm, B 64; `font` updates `columns()` |
| `text(s)` / `line(s)` | PC858 bytes (+ LF) | |
| `feed(n)` | `ESC d n` | clamped to 255; nothing for `n <= 0` |
| `lineSpacing(dots)` / `defaultLineSpacing()` | `ESC 3 n` / `ESC 2` | tighten the pitch for the dense legal block / restore |
| `rule(c)` | full-width row of `c` | |
| `paragraph(s)` | word-wrap to `columns()` | `wrapText` measures the folded text (an ellipsis prints as three dots) |
| `leftRight(l, r)` | label + right-justified value | one line |
| `garmentRow(qty, name, amt)` | 3-column row | the name wraps; an amount wider than its column is never cut: it is printed whole on its own line |
| `rasterImage(img)` | `GS v 0` | 1bpp, MSB-first, thresholded on luminance (never dithered); the QR goes out this way |
| `cut(n)` | `ESC d n` + `GS V 66 0` | feed, partial cut |

### Code page

`toCp858()` transcodes a `QString` to PC858 (CP850 plus the euro sign) with an explicit table, since Qt 6's `QStringConverter` has no CP858. ASCII passes through; the Spanish letters and `€`, the common extras (`² ³ ¹ × ± § ® µ`, Nordic and Portuguese letters, `ß ¢ ¥`) map via the table; typographic dashes, curly quotes, `…` and the no-break space fold to ASCII; anything else becomes `?`; C0 control characters (ESC, GS ...) become a space, so a typed name can never inject a printer command. The table is checked against reference CP858 values (`test_cp858TableAgainstReference`); confirming it on a printed code-page chart of the shop unit is still worthwhile.

## TicketRenderer

`static QByteArray render(const TicketData &d, int paperDots)`. `TicketData` is plain data (no DB, no widgets): `imprimir` fills it from the query model, tests feed synthetic rows. The block order is documented in [imprimir](imprimir.md#ticket-layout).

- Header lines are wrapped here (the shop name at half the columns, being double-size), not by the firmware.
- The factura IVA split is `splitGross` (the Verifactu record's math), so printed base + IVA add back to the total; the rate prints with its natural decimals (`IVA (21%)`, `IVA (10,5%)`).
- Verifactu fields of `TicketData`: `qr` (null = none), `verifactuVerifiable` (the leyenda, two Font A lines), `issueDate`, `voidedMarker` / `voidedDate` (heading and note, no QR), `timestamp` (supplied by the caller).
- `printableQr(image, paperDots)` is the QR sizing the Orden HAC/1177/2024 (art. 20-21) requires: it reads the symbol's modules from whatever pixmap it gets and redraws them at whole dots per module, 30-40 mm (240-320 dots), with a 4-module quiet zone, never wider than the paper. It returns a null image for a symbol it cannot read (`Imprimir` then prints the original as received).

## ThermalPrinter

`static bool send(const QByteArray &escpos, const QString &queueName, QString *err)`. Windows: `OpenPrinterW`, `StartDocPrinterW` (datatype `RAW`), `StartPagePrinter`, `WritePrinter`, `EndPagePrinter`, `EndDocPrinter`, `ClosePrinter`. An empty `queueName` uses `PrinterQueue::defaultName()`; an empty payload is refused. It returns `false` with a readable `*err` (from `FormatMessage`) instead of throwing. Off Windows it is a stub returning `false`, so builder, renderer and tests stay cross-platform.

## Status API path (optional)

Behind `print.use_status_api` (default off): reads device status around printing (paper out / near end, cover open, cutter / mechanical / unrecoverable error). The payload is identical to RAW, only the transport differs ([option B](printer/02_control_methods.md)).

- **`PrinterStatus`** (pure): `fromAsb(quint32)` decodes the Epson ASB word into named flags (`paperEnd`, `paperNearEnd`, `coverOpen`, `cutterError`, `mechanicalError`, `unrecoverableError`, `noResponse`, `offline`), plus `hasError()`, `hasWarning()`, `isFatal()` and a Spanish `summary()`. Fatal means cutter / mechanical / unrecoverable (block printing); cover open and paper end are recoverable (warn, let the spooler hold the job). `offline` is for diagnostics only.
- **`sendAndReadStatus(bytes, queue, *status, *err, gate)`** (Win32): loads the DLL dynamically, then `BiOpenMonPrinter(2, queue)`, `BiGetStatus` (read first), `BiDirectIOEx` (send), `BiCloseMonPrinter`. Status is read before sending because `BiGetStatus` returns the cached ASB even offline, whereas the send fails with `ERR_ACCESS` (-80) on an open cover. A fatal status skips the send and returns `false`. It returns `false` without sending if the DLL is missing or the queue cannot be opened, so the caller falls back to RAW. A `StatusApiSendGate` lets a watchdog stop a worker that has not started sending.
- **`sendAndReadStatusBounded(..., timeoutMs, &mayStillPrint)`**: runs the DLL calls on a detached worker and waits at most `timeoutMs` (`Imprimir` passes 10 s). On timeout it returns `false` with an invalid status and the API is latched off for the process, so later prints go straight to RAW. A worker abandoned before its send never sends; one abandoned inside the send sets `mayStillPrint` and the caller must not resend by RAW. The 10 s sits well above the real worst case (a send failing on an open cover or no paper returns after the DLL's own ~5 s, ~5.5 s with open and close), so a normal offline print never false-trips.
- **`runDiagnosticsBounded(queue, timeoutMs)`**: opens the printer and calls each API function in turn (`BiOpenMonPrinter`, `BiGetStatus`, `BiGetType`, `BiDirectIOEx` real-time status, `BiCloseMonPrinter`), logging `[diag]` before and after each with elapsed ms. A hang shows as the last `->` line with no result and a "BLOQUEO" note. Wired to the **Probar Status API (diagnóstico)** button (Settings, General tab).

`Imprimir::printTicket` decision: fatal status goes to a warning and stops (no RAW); a recoverable error or near-end warns and falls through to RAW (the spooler prints once the cover is closed or paper replaced); a refused send with a status that decoded to no known fault gives a generic "no se pudo enviar" warning and RAW; a clean send is done. A missing DLL leaves `status.valid == false`: silent, RAW only.

Hardware finding (shop TM-T20III with APD6): an open cover is reported as `ASB_NO_RESPONSE` (raw ASB `0x1`, `PrinterStatus::noResponse`, summary "La impresora no responde: tapa abierta, apagada o sin conexión"), not `ASB_COVER_OPEN`. The warning is therefore keyed off the send failure whenever a valid status was reached. The raw ASB word is logged for nonzero values; the specific-bit decode is kept for units that report it.

## Settings consumed (by `imprimir`)

| Key | Getter | Use |
|-----|--------|-----|
| `print.printer_name` | `printerName()` | RAW target queue; empty = default printer |
| `print.paper_width_mm` | `paperWidthMm()` | 80: 576 dots, 58: 420 (default 80) |
| `print.enable` | `enablePrinting()` | gates the actual send |
| `print.use_status_api` | `useStatusApi()` | route via the Status API (default off) |

All are editable in Settings, General.
