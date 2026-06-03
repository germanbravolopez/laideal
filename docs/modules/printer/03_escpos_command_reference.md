# 03 — ESC/POS Command Reference (the subset La Ideal needs)

ESC/POS is a byte protocol. Each command is a fixed marker byte (`ESC` = `0x1B`, `GS` = `0x1D`,
`FS` = `0x1C`, `DLE` = `0x10`) followed by a function byte and parameters. Printable text is
just sent as bytes in the **currently selected code page**. This file lists only the commands
the La Ideal receipt/invoice actually requires, with exact values; the full set is in Epson's
ESC/POS Command Reference.

Notation: hex bytes in `[ ]`; `n` is a single parameter byte.

## Initialisation and feed

| Purpose | Command | Bytes | Notes |
|---------|---------|-------|-------|
| Initialise printer | `ESC @` | `[1B 40]` | Resets all modes (font, align, size, spacing). Send first. |
| Print + line feed | `LF` | `[0A]` | Prints the line buffer and advances one line. The workhorse line terminator. |
| Feed n lines | `ESC d n` | `[1B 64 n]` | Advance `n` blank lines (used for bottom margin before cut). |
| Carriage return | `CR` | `[0D]` | Usually paired with LF; not needed if you only use LF. |
| Set line spacing | `ESC 3 n` | `[1B 33 n]` | Spacing = `n`/180 inch. `ESC 2` `[1B 32]` restores default (~30 dots). |

## Character style

| Purpose | Command | Bytes | Notes |
|---------|---------|-------|-------|
| Select font A/B | `ESC M n` | `[1B 4D n]` | `n`=0 Font A (12×24, 48 cols @80mm), `n`=1 Font B (9×17, 64 cols). |
| Bold on/off | `ESC E n` | `[1B 45 n]` | `n`=1 on, `n`=0 off. |
| Underline | `ESC - n` | `[1B 2D n]` | `n`=0 off, 1 = 1-dot, 2 = 2-dot. |
| Character size | `GS ! n` | `[1D 21 n]` | High nibble = width mag (0–7 ⇒ ×1–×8), low nibble = height mag. e.g. double both = `[1D 21 11]`. |
| Combined print mode | `ESC ! n` | `[1B 21 n]` | Bitfield: `0x01` font B, `0x08` bold, `0x10` double-height, `0x20` double-width, `0x80` underline. One-shot alternative to the above. |
| Reverse (white on black) | `GS B n` | `[1D 42 n]` | `n`=1 on, 0 off. Handy for a header band. |

## Alignment and horizontal position

| Purpose | Command | Bytes | Notes |
|---------|---------|-------|-------|
| Justify | `ESC a n` | `[1B 61 n]` | `n`=0 left, 1 center, 2 right. Affects whole lines. |
| Absolute position | `ESC $ nL nH` | `[1B 24 nL nH]` | Move to dot column `nL + nH*256` from left. Use for a money column right-edge. |
| Set left margin | `GS L nL nH` | `[1D 4C nL nH]` | Left margin in dots (page start offset). |
| Set print area width | `GS W nL nH` | `[1D 57 nL nH]` | Printable width in dots. |
| Horizontal tab | `HT` + `ESC D ...` | `[09]`, `[1B 44 …00]` | Tab stops set by `ESC D`. Column tables are usually easier done with space-padding in a monospace font than with tab stops. |

> COLUMN LAYOUT TIP: Font A and Font B are monospace. With 48 (Font A) or 64 (Font B) columns at
> 80 mm — 35 / 46 at 58 mm — the simplest robust table is to **space-pad fields to fixed column
> widths** in the host code and print left-justified, rather than juggling tab stops. Right-align
> the amount column by padding on the left. This mirrors how the current Excel layout splits into
> `Uds. | Prendas | Importe`.

## Code pages — Spanish text (critical)

ESC/POS sends text as **single-byte code-page bytes**, not UTF-8. The receipt contains `ñ á é í
ó ú ¿ ¡ €`, so the host must (1) select a code page on the printer and (2) transcode each
`QString` to that code page's bytes.

| Purpose | Command | Bytes | Notes |
|---------|---------|-------|-------|
| Select code page | `ESC t n` | `[1B 74 n]` | Picks the character table. Common: `n`=2 PC850 (Multilingual, has accents), `n`=19 PC858 (PC850 + €), `n`=16 WPC1252 (Windows-1252). |
| International charset | `ESC R n` | `[1B 52 n]` | Per-country glyph swaps; `n`=7 = Spain. Usually leave default and rely on the code page. |

Recommended: select **PC858** (`ESC t 19`) — it is Western-European with the `€` sign — and
transcode `QString` → CP858 in C++ (`QStringEncoder("CP858")` in Qt 6, or `QTextCodec` in Qt 5;
or a small explicit lookup table for the ~12 glyphs actually used). Verify on the physical unit:
the exact code-page numbering can vary, and the safest path is to print a self-test / code-page
chart from the printer and confirm. This transcoding step is the single most error-prone part of
the rewrite — Excel handled it invisibly, ESC/POS does not.

## Paper cut

| Purpose | Command | Bytes | Notes |
|---------|---------|-------|-------|
| Partial cut | `GS V m` | `[1D 56 01]` (or `00` full) | Cut at the current position. The T20III does a partial cut (one point left uncut). |
| Feed + partial cut | `GS V m n` | `[1D 56 42 n]` | `m`=66 (`0x42`): feed `n` dots then partial cut. Cleaner — guarantees the footer clears the cutter before cutting. |

Typical end-of-receipt: `ESC d 3` (feed a few lines so the printed area clears the cutter) then
`GS V 66 0` (feed + partial cut), or simply `GS V 1`.

## Raster image (logo + Verifactu QR via pixmap)

| Purpose | Command | Bytes | Notes |
|---------|---------|-------|-------|
| Print raster bit image | `GS v 0 m xL xH yL yH d…` | `[1D 76 30 m xL xH yL yH <data>]` | `m`=0 normal. `xL,xH` = bytes per row (= ceil(widthDots/8)), `yL,yH` = height in dots. Data is 1bpp, MSB-first, row-major, 1 = black dot. |
| (newer) Store+print graphics | `GS ( L` / `GS 8 L` | `[1D 28 4C …]` / `[1D 38 4C …]` | Function 112/48/50: define and print a raster. Supports larger images than `GS v 0`. |
| Register NV logo | `GS ( L` (fn 67/64) | `[1D 28 4C …]` | One-time: store the shop logo in NV memory, then recall it per ticket with a short command instead of resending the bitmap. |

For the **Verifactu QR**, the existing code already obtains a `QPixmap` from
`VerifactuIntegration` (the exact AEAT-issued QR). The most faithful approach is to convert that
`QPixmap` to a 1-bpp raster and emit it with `GS v 0`. (Recommended over re-encoding — see the
native-QR option below and the trade-off in [`05_implementation_plan.md`](05_implementation_plan.md).)

Conversion sketch (host side): `pixmap.toImage().convertToFormat(QImage::Format_Mono)`, then for
each row pack 8 horizontal pixels per byte (MSB = leftmost), `1` = black. Scale to ~140–200 dots
wide so it is scannable but fits the roll.

## Native 2D / barcodes (firmware-generated, optional)

The firmware can generate the QR itself, avoiding host rastering. Five-step `GS ( k` sequence
(`GS ( k` = `[1D 28 6B]`, `cn`=`49`/`0x31` selects QR):

| Step | Command | Bytes |
|------|---------|-------|
| 1. Select model | fn 65 | `[1D 28 6B 04 00 31 41 n1 n2]` (`n1`=50 model 2, `n2`=0) |
| 2. Module size | fn 67 | `[1D 28 6B 03 00 31 43 n]` (`n`=1–16 dot size, e.g. 4–8) |
| 3. Error correction | fn 69 | `[1D 28 6B 03 00 31 45 n]` (`n`=48 L,49 M,50 Q,51 H) |
| 4. Store data | fn 80 | `[1D 28 6B pL pH 31 50 30 <data…>]` where `len = dataLen+3`, `pL = len & 0xFF`, `pH = len >> 8` |
| 5. Print | fn 81 | `[1D 28 6B 03 00 31 51 30]` |

1D barcodes (if ever needed, e.g. encoding `n_recibo`): `GS k m …` `[1D 6B m …]`, with
`GS h n` `[1D 68 n]` for height and `GS w n` `[1D 77 n]` for module width.

## Status (optional, for the Status-API enhancement)

| Purpose | Command | Bytes | Notes |
|---------|---------|-------|-------|
| Real-time status | `DLE EOT n` | `[10 04 n]` | `n`=1 printer status, 2 offline cause, 3 error cause, 4 paper-roll sensor. Returns 1 byte immediately (needs a bidirectional channel). |
| Transmit status | `GS r n` | `[1D 72 n]` | `n`=1 paper sensor, 2 drawer. |
| Cash drawer kick | `ESC p m t1 t2` | `[1B 70 m t1 t2]` | `m`=0/1 connector pin 2/5, `t1`/`t2` pulse timing. (Or Status API `BiOpenDrawer`.) |

Reading status back requires a bidirectional transport. Through the RAW spooler this is not
reliably available, which is why the **Status API (option B)** is the clean way to get status —
see [`02_control_methods.md`](02_control_methods.md).

## Status API functions (option B reference)

From `APD6_Status_en_revD.pdf` (Win32). Header `EPSStmApi.h`, DLL `EPSStmApi.dll`.

| Function | Purpose |
|----------|---------|
| `BiOpenMonPrinter(nType, pName)` | Open by port (`nType`=1, e.g. `"ESDPRT001"`) or printer name (`nType`=2, e.g. `"EPSON TM-T20III Receipt"`). Returns handle (negative = error). |
| `BiCloseMonPrinter(handle)` | Close. Always pair with open. |
| `BiLockPrinter` / `BiUnlockPrinter` | Exclusive access (shared/multi-process). |
| `BiDirectIOEx(handle, writeLen, writeCmd, &readLen, readBuff, timeout, nullTerminate, option)` | **Send ESC/POS** and optionally read the device reply. The ESC/POS payload is identical to what RAW mode sends. |
| `BiGetStatus(handle, &status)` | Current ASB status: online/offline, cover open, paper near-end/end, error. |
| `BiSetStatusBackFunction(Ex)` / `BiCancelStatusBack` | Register a callback fired when status changes (async paper-out notification). |
| `BiOpenDrawer(...)` | Cash-drawer kick. |
| `BiResetPrinter` / `BiForceResetPrinter` | Reset USB/parallel/Ethernet interface printers. |
| `BiGetType` / `BiGetPrnCapability` | Model info / firmware / capabilities. |

Error codes are negative (`-10` param, `-30` no driver, `-40` unavailable, `-70` timeout,
`-80` access, `-90` param, `-1000` locked, …); macros in `EPSStmApi.h`.

## A worked example — minimal receipt skeleton

Conceptual byte stream for a tiny centered-header + one line + cut (host pseudocode):

```
ESC @                         1B 40                 ; init
ESC t 19                      1B 74 13              ; code page PC858 (€, accents)
ESC a 1                       1B 61 01              ; center
GS ! 0x11                     1D 21 11              ; double width+height
"LA IDEAL\n"                  ... 0A                ; business name (CP858 bytes)
GS ! 0x00                     1D 21 00              ; normal size
ESC a 0                       1B 61 00              ; left
"Nº: 12345\n"                 ... 0A
... garment lines, totals, QR raster ...
ESC d 3                       1B 64 03              ; feed clear of cutter
GS V 66 0                     1D 56 42 00           ; feed + partial cut
```

## References

- Epson ESC/POS Command Reference — `https://support.epson.net/publist/reference_en/?ref=escpos`
  (the legacy `reference.epson-biz.com` mirror was retired 2024-06-16).
- TM-T20III model command list — `download4.epson.biz/.../escpos/tmt20iii.html`.
- `APD6_Status_en_revD.pdf` — Status API Win32 reference (Chapter 3).
- mike42.me "What is ESC/POS"; liana-ali.blogspot.com QR `GS ( k` walk-through; Pyramid ESC/POS
  command table (`escpos.readthedocs.io`).
