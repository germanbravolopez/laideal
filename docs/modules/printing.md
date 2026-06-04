# printing (`src/printing/`)

Direct **ESC/POS** thermal printing for the Epson TM-T20III (and any ESC/POS
printer). This library replaced the old Excel/QXlsx/`cscript` path: it builds the
receipt as a byte stream and sends it **RAW** through the Windows print spooler —
no Excel, no QXlsx, no COM, no `.vbs`, no Epson SDK (only `winspool`, which ships
with Windows). Background and design: [`printer/`](printer/README.md); a rendered
[sample recibo + factura](printer/README.md#sample-rendered-output) (PNG, from the
`test_ticket_preview` output) shows what the layout looks like.

## Source files

- `escposbuilder.h/cpp` — pure ESC/POS byte builder + PC858 transcode
- `ticketrenderer.h/cpp` — `TicketData` → receipt/invoice layout
- `thermalprinter.h/cpp` — Win32 RAW spooler transport (stub off Windows)
- `printerstatus.h/cpp` — pure ASB status-word decoder (`PrinterStatus`)
- `statusapiprinter.h/cpp` — optional Epson Status API transport (dynamically
  loaded `EPSStmApi.dll`; stub off Windows)

Built as the static lib `printing` (links `Qt::Gui` for `QImage`, and `winspool`
on Windows). `imprimir` links it; nothing else depends on it.

## Three pieces with clean seams

```
EscPosBuilder   pure: accumulates a QByteArray of ESC/POS commands. No I/O, no
                GUI event loop -> fully unit-testable (assert exact bytes).
TicketRenderer  pure: maps a TicketData (header fields, garment rows, totals,
                QR image, flags) onto an EscPosBuilder -> same input, same bytes.
ThermalPrinter  transport: writes a QByteArray to a Windows printer queue as RAW
                spool data. The only host-specific, non-portable piece.
```

`EscPosBuilder`, `TicketRenderer` and `PrinterStatus` are covered by
`tests/test_escpos.cpp` (exact control bytes, the PC858 transcode, the
column-width math, the rendered receipt/invoice fragments, and the ASB status
decode). `ThermalPrinter` and `StatusApiPrinter` are integration-only (Win32 /
Epson DLL).

## EscPosBuilder

Chainable builder. Tracks the current font + paper width so `columns()` returns
the usable character count (dots / char width: Font A = 12 dots, Font B = 9), and
the padding helpers space-pad to that width.

| Method | ESC/POS | Notes |
|--------|---------|-------|
| `init()` | `ESC @` + `ESC t 19` | reset + select code page PC858 |
| `align(a)` | `ESC a n` | Left / Center / Right |
| `bold(on)` | `ESC E n` | |
| `font(f)` | `ESC M n` | A (48 cols @80mm) / B (64); updates `columns()` |
| `doubleSize(on)` | `GS ! 0x11` / `0x00` | double width+height |
| `text(s)` / `line(s)` | PC858 bytes (+ LF) | transcodes the QString |
| `feed(n)` | `ESC d n` | |
| `lineSpacing(dots)` / `defaultLineSpacing()` | `ESC 3 n` / `ESC 2` | tighten the vertical pitch (e.g. the dense legal block) / restore the ~1/6" default |
| `rule(c)` | full-width row of `c` | `=` (double-ish) / `-` (thin) |
| `paragraph(s)` | word-wrap to `columns()` | |
| `leftRight(l, r)` | label + right-justified value | one full line |
| `garmentRow(qty, name, amt)` | 3-column table row | name wraps; continuation rows blank qty/amt |
| `rasterImage(img)` | `GS v 0` | 1bpp, MSB-first, threshold on luminance |
| `cut(n)` | `ESC d n` + `GS V 66 0` | feed clear of cutter, partial cut |
| `raw(bytes)` | — | escape hatch |

### Code page (the error-prone bit)

ESC/POS prints single-byte code-page bytes, not UTF-8. `EscPosBuilder::toCp858()`
transcodes a `QString` to **PC858** (CP850 + the euro sign), selected on the
device with `ESC t 19`. ASCII passes through; the ~50 Spanish glyphs actually
used (`ñ Ñ á é í ó ú ¿ ¡ €` + accented capitals, etc.) map via an explicit table;
anything else becomes `?`. An explicit table (not Qt's `QStringConverter`, which
in Qt 6 supports only UTF/Latin-1, **not** CP858) keeps it correct and unit-tested.
The exact PC858 numbering should still be confirmed against a printed code-page
chart on the physical unit.

## TicketRenderer

`static QByteArray render(const TicketData &d, int paperDots)`. `TicketData` is
plain data (no DB, no widgets) so `imprimir` assembles it from the query model
and tests feed synthetic rows. Reproduces the historical Excel layout (header →
identity → garment table → totals → payment/copy marker → QR → legal policy →
timestamp → cut) within the printer's column width. The factura IVA split
reproduces the legacy rounding (IVA derived from the rounded base string) so the
printed base + IVA add back to the total.

## ThermalPrinter

`static bool send(const QByteArray &escpos, const QString &queueName, QString *err)`.
On Windows: `OpenPrinterW` → `StartDocPrinterW` (datatype `RAW`) →
`StartPagePrinter` → `WritePrinter` → `EndPagePrinter` → `EndDocPrinter` →
`ClosePrinter`. An empty `queueName` uses `GetDefaultPrinterW`. Returns `false`
with a human-readable `*err` (from `FormatMessage`) instead of throwing. Off
Windows it compiles to a stub that returns `false`, so the builder/renderer (and
their tests) stay cross-platform.

## PrinterStatus + StatusApiPrinter (optional Status API path)

An opt-in layer (setting `print.use_status_api`) that reads device status after
printing — paper out / near end, cover open, cutter / mechanical / unrecoverable
error. The ESC/POS payload is identical to the RAW path; only the transport
differs (dossier option B, [`printer/02_control_methods.md`](printer/02_control_methods.md)).

- **`PrinterStatus`** — pure. `PrinterStatus::fromAsb(quint32)` decodes the
  Epson **ASB** (Automatic Status Back) 32-bit word into named flags
  (`paperEnd`, `paperNearEnd`, `coverOpen`, `cutterError`, …) plus
  `hasError()` / `hasWarning()` and a Spanish `summary()` ("Sin papel", "Tapa
  abierta", "Papel casi agotado", "Impresora lista", …). Unit-tested.
- **`StatusApiPrinter::sendAndReadStatus(bytes, queue, *status, *err)`** —
  Win32. Loads `EPSStmApi.dll` **dynamically** (`LoadLibrary`/`GetProcAddress`,
  so there is no build-time SDK dependency), then
  `BiOpenMonPrinter(2, queue)` → `BiDirectIOEx` (send) → `BiGetStatus` (read) →
  `BiCloseMonPrinter`. Returns `false` *without sending* if the DLL/API is
  unavailable or the queue can't be opened, so `Imprimir::printTicket` falls
  back to the RAW path (enabling the flag never stops printing). The status read
  is best-effort: a send can succeed with `status.valid == false`.

> Not hardware-validated: the exact `ASB_*` bit values and the `EPSStmApi.dll`
> calling path must be confirmed on the shop's TM-T20III with the APD/Status API
> installed (the same Phase 0-style hardware step the RAW path went through).

## Settings consumed (by `imprimir`)

| Setting | Getter | Use |
|---------|--------|-----|
| `print.printer_name` | `printerName()` | RAW target queue; empty = default printer |
| `print.paper_width_mm` | `paperWidthMm()` | 80 → 576 dots, 58 → 420 (default 80) |
| `print.enable` | `enablePrinting()` | gates the actual send |
| `print.use_status_api` | `useStatusApi()` | route via Epson Status API + read status (default off) |

All keys are editable in `SettingsDialog` → General (printer combo populated
from `QPrinterInfo::availablePrinterNames()`, paper-width combo, and the Status
API checkbox).
