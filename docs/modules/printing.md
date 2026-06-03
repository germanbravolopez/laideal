# printing (`src/printing/`)

Direct **ESC/POS** thermal printing for the Epson TM-T20III (and any ESC/POS
printer). This library replaced the old Excel/QXlsx/`cscript` path: it builds the
receipt as a byte stream and sends it **RAW** through the Windows print spooler —
no Excel, no QXlsx, no COM, no `.vbs`, no Epson SDK (only `winspool`, which ships
with Windows). Background and design: [`printer/`](printer/README.md).

## Source files

- `escposbuilder.h/cpp` — pure ESC/POS byte builder + PC858 transcode
- `ticketrenderer.h/cpp` — `TicketData` → receipt/invoice layout
- `thermalprinter.h/cpp` — Win32 RAW spooler transport (stub off Windows)

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

`EscPosBuilder` and `TicketRenderer` are covered by `tests/test_escpos.cpp`
(exact control bytes, the PC858 transcode, the column-width math, and the
rendered receipt/invoice fragments). `ThermalPrinter` is integration-only.

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

## Settings consumed (by `imprimir`)

| Setting | Getter | Use |
|---------|--------|-----|
| `print.printer_name` | `printerName()` | RAW target queue; empty = default printer |
| `print.paper_width_mm` | `paperWidthMm()` | 80 → 576 dots, 58 → 420 (default 80) |
| `print.enable` | `enablePrinting()` | gates the actual send |

Both new keys are editable in `SettingsDialog` → General (printer combo populated
from `QPrinterInfo::availablePrinterNames()`, paper-width combo).
