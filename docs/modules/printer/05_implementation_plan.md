# 05 — Implementation Plan

Proposed design for replacing the Excel/QXlsx/cscript path with direct ESC/POS over the RAW
Windows spooler. This is a plan, not shipped code. It is written to fit the project's
conventions: Qt 5/6 + C++17, CMake module libraries under `src/`, free-function/pure-helper
seams for unit tests (see the Qt Test suites in [`../../../tests`](../../../tests)), English-only docs.

## Goals and non-goals

**Goals**
- Remove the Excel + QXlsx + `.vbs` + `cscript` dependency entirely.
- Print recibos and facturas byte-for-byte equivalent in content/ordering to today
  (see [`04_current_printing_flow.md`](04_current_printing_flow.md)).
- Keep the `Imprimir` call sites working with minimal change.
- Make the layout driven by the printer's real column width (config: 58/80 mm).
- Add unit-testable seams (the ESC/POS byte builder is pure).

**Non-goals (deferrable)**
- Real-time status/paper-out detection (the optional Status API layer, Phase 4).
- Cash-drawer / buzzer control.
- NV-logo registration (nice-to-have once the basics work).

## Proposed module shape

Three small pieces with clean seams. Keep them in `src/imprimir/` (or a new `src/printing/`
library) so `imprimir` stops depending on `QXlsx`.

```
EscPosBuilder      pure: accumulates a QByteArray of ESC/POS commands. No Qt GUI, no I/O.
                   → fully unit-testable (assert exact bytes), like test_sql_lite.

ThermalPrinter     transport: sends a QByteArray to a Windows queue via RAW spooler.
                   → thin Win32 wrapper (winspool). Manual/integration verified.

TicketRenderer     maps ticket data (the QSqlQueryModel rows + settings + QR pixmap) into
                   EscPosBuilder calls. Reproduces the layout in 04. Logic testable by
                   feeding fake rows and asserting the emitted byte stream.
```

`Imprimir` keeps its public interface; internally `createTicketExcel(...)` is replaced by
`buildTicket(...)` (fills an `EscPosBuilder`) and `printTicket()` calls `ThermalPrinter::send()`.

### EscPosBuilder (sketch)

```cpp
class EscPosBuilder {
public:
    explicit EscPosBuilder(int paperDots = 576);   // 576 @80mm, 420 @58mm
    EscPosBuilder& init();                          // ESC @  + ESC t <codepage>
    EscPosBuilder& align(Align a);                  // ESC a
    EscPosBuilder& bold(bool on);                   // ESC E
    EscPosBuilder& font(Font f);                    // ESC M  (A=48 cols, B=64 cols @80mm)
    EscPosBuilder& size(int wMag, int hMag);        // GS !
    EscPosBuilder& text(const QString& s);          // transcode → code page bytes
    EscPosBuilder& line(const QString& s = {});     // text + LF
    EscPosBuilder& columns(const QString& l, const QString& r);  // left + right-justified amount
    EscPosBuilder& rule(bool doubleLine = false);   // a row of '=' or '-'
    EscPosBuilder& feed(int n);                      // ESC d n
    EscPosBuilder& rasterImage(const QImage& mono);  // GS v 0  (for the QR pixmap / logo)
    EscPosBuilder& cut();                            // ESC d 3 + GS V 66
    QByteArray bytes() const;
private:
    QByteArray m_buf;
    int m_paperDots;
    int m_cols;            // derived from paper width + current font
};
```

Column count derives from `m_paperDots` and the font (48/64 @80mm, 35/46 @58mm), so
`columns()` can space-pad to the right width regardless of paper size.

### ThermalPrinter (transport, sketch)

```cpp
class ThermalPrinter {
public:
    // queueName empty → use GetDefaultPrinter()
    static bool send(const QByteArray& escpos, const QString& queueName, QString* err = nullptr);
};
```

Implementation = the Win32 RAW sequence from [`02_control_methods.md`](02_control_methods.md):
`OpenPrinterW` / `StartDocPrinterW`(datatype `RAW`) / `StartPagePrinter` / `WritePrinter` /
`EndPagePrinter` / `EndDocPrinter` / `ClosePrinter`, with error text from `GetLastError()`.
Returns false (and `*err`) instead of throwing, so callers can show a `QMessageBox` like today.

> Build: link `winspool` (`target_link_libraries(... winspool)` / `-lwinspool`). No new
> third-party dependency. Guard the file with `#ifdef Q_OS_WIN` — non-Windows builds (the CI
> test runner) compile a stub that returns false, so `EscPosBuilder` tests stay cross-platform.

### Code-page transcoding (the risky bit)

Add a `QString → CP858` (or chosen page) encoder used by `EscPosBuilder::text()`:
- Qt 6: `QStringEncoder enc("CP858"); QByteArray b = enc.encode(s);`
- Qt 5: `QTextCodec::codecForName("IBM 858")->fromUnicode(s);`
- Fallback: an explicit lookup table for the ~12 glyphs actually used
  (`ñ Ñ á é í ó ú ¿ ¡ € ª º` and accented capitals), with `?` for anything unmapped.

Pick the code page (`ESC t n`) to match the encoder. **PC858** (`ESC t 19`) is the safe default
(Western European + `€`). Validate against a printed code-page chart on the physical unit.

### Verifactu QR

Reuse `Imprimir::resolveQrCode()` unchanged (async fetch, 5 s wait, AEAT-exact pixmap). Render
the returned `QPixmap` via `EscPosBuilder::rasterImage()`:
`qr.toImage().convertToFormat(QImage::Format_Mono)`, scale to ~160 dots wide, pack 1bpp MSB-first.
Rastering the existing pixmap (vs native `GS ( k` re-encoding) guarantees the printed QR is
exactly what AEAT issued. Keep native `GS ( k` as a documented fallback only.

## Settings to add

| Key | Getter | Default | UI |
|-----|--------|---------|-----|
| `print.printer_name` | `printerName()` | empty = use default printer | `SettingsDialog` text field + a "detect installed printers" combo (enumerate via `EnumPrinters`). |
| `print.paper_width_mm` | `paperWidthMm()` | 58 (confirm on hardware) | combo 58 / 80. |
| `print.enable` | `enablePrinting()` | existing | existing checkbox. |

`ticketExcelPath()` / `ticketPrintScriptPath()` are removed.

## Phased delivery

1. **Phase 0 — Spike (throwaway).** From a tiny harness, `ThermalPrinter::send()` a hand-built
   `ESC @ … "La Ideal" LF … GS V` to the real queue. Confirm: queue name, that RAW reaches the
   device, code page for accents, and the cut. **De-risks everything before touching `Imprimir`.**
2. **Phase 1 — `EscPosBuilder` + `ThermalPrinter`.** Implement both with the code-page encoder.
   Add `tests/test_escpos_builder.cpp` asserting exact bytes for init/align/bold/columns/cut and
   the CP858 transcode of a Spanish string. (Mirrors the existing pure-helper test pattern.)
3. **Phase 2 — `TicketRenderer`.** Reproduce the full layout (recibo + factura + complete
   invoice, totals, copy markers, QR + legend, policy, timestamp) from
   [`04_current_printing_flow.md`](04_current_printing_flow.md). Test by feeding synthetic rows
   and asserting key fragments in the output.
4. **Phase 3 — Wire into `Imprimir`; delete Excel path.** Swap `createTicketExcel`→`buildTicket`
   and `printTicket`→`ThermalPrinter::send`; add the two settings; drop the `.xlsx`/`.vbs`/
   `cscript` code and the `QXlsx` link from `src/imprimir/CMakeLists.txt`. Verify every caller in
   the integration table still compiles and prints. **Side-by-side check**: keep a hidden
   "export xlsx" debug path for one release if a visual diff against the old output is wanted.
5. **Phase 4 — (optional) Status API layer.** Behind `print.use_status_api`, route the same
   bytes through `BiDirectIOEx` and surface `BiGetStatus` paper-out/cover-open in the status bar.
6. **Phase 5 — Cleanup.** If no module other than `imprimir` used `QXlsx`, remove the vendored
   `QXlsx/` and its CMake wiring (and update the file map / "Excel library" row in the docs).
   Run `/update-docs`; move the progress-tracker issue to Completed Milestones.

## Testing strategy

- **Unit (CI, headless)**: `EscPosBuilder` byte assertions + code-page transcode + column
  padding/width math. Pure, cross-platform, fits the existing `ctest` gate.
- **Integration (manual, on the shop PC)**: print a recibo, a factura (paid), a complete invoice,
  a multi-seq factura chooser, and a Verifactu factura with a real QR; confirm cut, accents, QR
  scans, column fit at the real paper width, and the establishment-copy / second-copy prompts.
- **Regression**: keep the old Excel output of a few representative tickets as reference images
  for a one-time visual diff during Phase 3/4.

## Risks and mitigations

| Risk | Mitigation |
|------|-----------|
| Accents/€ wrong (code page) | Phase 0 validates the code page on the real unit; explicit fallback table; unit-test the transcode. |
| Wrong/relocated printer queue name | Default to `GetDefaultPrinter()`; offer an `EnumPrinters` picker in settings. |
| Paper width assumption (58 vs 80) | Make it a setting; derive columns from dots; confirm on hardware in Phase 0. |
| QR not scannable / wrong content | Raster the exact AEAT pixmap (not re-encode); test scan with a phone; size ~160 dots. |
| RAW gives no "did it print?" signal | Acceptable parity with today (Excel gave none either); Phase 4 Status API adds real detection. |
| A site has no thermal printer | `enablePrinting()` already gates printing; with Excel gone there is no file artifact — if file export is still wanted, keep the optional debug xlsx export (Phase 3 note) or write a plain-text dump. |
| Non-Windows CI build | `ThermalPrinter` behind `#ifdef Q_OS_WIN` with a stub; builder/renderer stay portable and tested. |

## Definition of done

- `Imprimir` prints recibos/facturas with no Excel, QXlsx, `.vbs`, or `cscript` involved.
- All six call sites in [`04_current_printing_flow.md`](04_current_printing_flow.md) work.
- Output matches today's content/ordering at the configured paper width, accents and QR correct.
- `EscPosBuilder` unit tests pass in the `ctest` gate.
- Docs updated (`/update-docs`): file map "Print + Excel generation" row, architecture module
  entry, and the progress-tracker issue moved to Completed Milestones.

## References

- [`02_control_methods.md`](02_control_methods.md) — transport choice and Win32 RAW sequence.
- [`03_escpos_command_reference.md`](03_escpos_command_reference.md) — the exact bytes for each builder method.
- [`04_current_printing_flow.md`](04_current_printing_flow.md) — layout + integration surface to preserve.
- [`../../../tests`](../../../tests) — the Qt Test + CTest pattern the new unit tests follow.
- `/coding-guidelines` skill — language/Qt/DB conventions for the new code.
