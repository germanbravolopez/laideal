# 01 — Printer Model and Specifications

## Model identification

The printer is an **Epson TM-T20III** thermal line receipt printer. This is confirmed by every
document in the local documentation folder (`APD_604_T20III_WM`, `APD6_Spec_T20III_en_revA.pdf`,
`TM-T20III_trg_en_revD.pdf`, `TM-T20III_ug_ww_01.pdf`) and matches Epson's current product line.

- **Family**: TM-T20 series, third generation (TM-T20III). Successor to the TM-T20II.
- **Command system**: Epson **ESC/POS** (proprietary, the de-facto POS standard).
- **Product codes** (interface depends on the model purchased): `C31CH51001` (USB + Serial),
  `C31CH51A9972` / Ethernet variants. The local SDK folder is the T20III driver package, so the
  unit is a genuine TM-T20III, not a "compatible".

### Which interface variant?

The TM-T20III is sold in two interface variants — **(a) USB + Serial (RS-232C)** or
**(b) Ethernet** — selectable at purchase. This matters because **ePOS-Print XML is only
available on the Ethernet model**, while **ESC/POS works on every variant**. The presence of
the **APD (Windows driver) + Status API** packages in the local folder, and the current
"print to the default Windows printer" flow, strongly indicate the shop uses the
**USB-connected** unit through the installed Windows queue.

> ACTION FOR IMPLEMENTATION: confirm the physical connection (USB vs Ethernet) and the exact
> installed Windows printer-queue name (Control Panel → Devices and Printers). The default APD
> queue name follows the pattern `EPSON TM-T20III Receipt`. This name (or its port, e.g.
> `ESDPRT001`) is the only host-specific value the new code needs. The recommended approach
> (RAW spooler, see [`02_control_methods.md`](02_control_methods.md)) works regardless of USB
> vs Ethernet, because it writes to the Windows queue, not the wire.

## Print specifications (the numbers that drive layout)

From the Technical Reference Guide (`TM-T20III_trg_en_revD.pdf`) and the APD Printer
Specification (`APD6_Spec_T20III_en_revA.pdf`):

| Spec | 80 mm paper | 58 mm paper |
|------|-------------|-------------|
| Dot density | 203 × 203 dpi (8 dots/mm) | 203 × 203 dpi |
| Printable width | 72.0 mm = **576 dots** | 52.5 mm = **420 dots** |
| Left / right margin | ~3.0 mm / ~4.5 mm | ~3.0 mm / ~2.0 mm |
| Characters/line, Font A (12×24) | **48** | 35 |
| Characters/line, Font B (9×17) | **64** | 46 |
| Default line spacing | 3.75 mm (30 dots), programmable | same |
| Max print speed | 250 mm/s (100 mm/s for barcodes/2D) | same |

- **Fonts**: Font A = 12 × 24 dots (default), Font B = 9 × 17 dots (smaller, more columns).
  Characters can be scaled up to 64× via `GS !` / `ESC !`.
- **Paper**: 80 mm or 58 mm roll, switchable. A 58 mm guide ships in the box. Max roll diameter
  83 mm. Specified thermal paper only.
- **Auto-cutter**: partial cut (one point left uncut), ~1.5 million cuts rated.
- **Buffers**: receive buffer 4 KB / 45 bytes (memory-switch selectable); NV graphics 256 KB;
  downloaded graphics 12 KB. Enough to register a shop logo in NV memory once and recall it per
  ticket.

> PAPER WIDTH NOTE: `progress_tracker.md` (issue "Improve thermal ticket / invoice layout")
> records the current ticket as **58 mm width**, and the Excel column widths were tuned
> empirically against the physical printer. Treat paper width as a **configurable** value in
> the new module (default to whatever the shop actually loads), and compute the column layout
> from the dot width (420 vs 576) rather than hard-coding character counts. Confirm the loaded
> roll width on the real unit before finalising the layout.

## Barcode / 2D symbol support (built into the firmware)

The TM-T20III prints these **natively** via ESC/POS — no host-side rasterization needed:

- 1D barcodes: UPC-A, UPC-E, EAN/JAN-8, EAN/JAN-13, CODE39, ITF, CODABAR (NW-7), CODE93,
  CODE128, GS1-128, and the full GS1 DataBar family.
- 2D symbols: **PDF417, QR Code, MaxiCode, Composite, Aztec Code, DataMatrix**.

This means the **Verifactu QR** can be produced either by the firmware (native `GS ( k`) or by
rastering the existing `QPixmap` from `VerifactuIntegration`. See
[`03_escpos_command_reference.md`](03_escpos_command_reference.md) and
[`05_implementation_plan.md`](05_implementation_plan.md) for the trade-off (we lean toward
rastering the existing pixmap for byte-exact fidelity with what AEAT issued).

## Status / error reporting

The firmware exposes printer state (online/offline, cover open, paper near-end/end, cutter
error, recoverable vs unrecoverable errors) via ESC/POS real-time status (`DLE EOT`, `GS r`)
and via the Epson Status API's **ASB (Automatic Status Back)**. The current Excel flow has
**no** status feedback at all (it cannot tell if paper ran out). Direct control lets us add
this later — see the optional Status API layer in
[`02_control_methods.md`](02_control_methods.md).

## Local documentation inventory

Everything below lives under `C:\Users\gebra\work\tintoreria\impresora`. Kept here as a map so
a future developer knows what each archive is for.

| Item | What it is | Relevance |
|------|-----------|-----------|
| `TM-T20III_trg_en_revD.pdf` | **Technical Reference Guide** (Rev. D) | Primary source: specs, interfaces, control methods, setup, software list, appendix. |
| `TM-T20III_ug_ww_01.pdf` | User's Guide | End-user handling (paper load, cleaning). Mostly images; little for development. |
| `APD_604_T20III_WM/` | **APD 6.04** (Advanced Printer Driver) for the T20III + installer + manuals | The Windows printer driver. Installs the `EPSON TM-T20III Receipt` queue we print through. Includes `APD6_Spec_T20III_en_revA.pdf` (model spec: resolution, margins, device fonts) and `APD6_Printer_en_revA.pdf` (driver settings). |
| `APD6_StatusAPI_I_WM/` | **Status API** package + `APD6_Status_en_revD.pdf` | The Epson DLL (`EPSStmApi.dll` / `EPSStmApi.h`) that monitors status and sends ESC/POS (`BiDirectIOEx`). Optional enhancement, not required for the core rewrite. |
| `APD_SampleProgram_EN_D/` | Sample programs (C++, C#, VB.NET; TM and DM display) | Reference code. The C++ TM samples are MFC apps that print via **GDI device fonts** through the APD driver (`CreateDC` + `TextOut`), i.e. the raster-driver path — useful to understand, not the path we recommend. |
| `OPOSADK/` | EPSON OPOS ADK V3.00ER18 (OCX/COM driver + samples) | The UnifiedPOS/OLE path. Aimed at VB/.NET via COM. Not a clean fit for a Qt/C++ app — see [`02_control_methods.md`](02_control_methods.md). |

## References

- `TM-T20III_trg_en_revD.pdf` — Product Overview, Application Development Information, Appendix
  (Product/Printing/Character/Paper Specifications, Printable Area).
- `APD6_Spec_T20III_en_revA.pdf` — Printer Driver resolution / paper sizes / margins / device fonts.
- Epson TM-T20III product & specification pages (epson.com / epson.eu) — interface variants,
  print speed, software list.
