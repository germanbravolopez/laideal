# 02 — Ways to Talk to the Printer

The TM-T20III can be driven five different ways. This file lays them out, compares them for
**our** context (a Qt 5/6 + C++17 Windows desktop app, MinGW build, printer on a USB queue),
and explains the choice.

## The five options

### A. Direct ESC/POS over the Windows spooler in RAW mode  ← RECOMMENDED

Build the receipt as a `QByteArray` of ESC/POS commands and hand it to the Windows print
spooler as **RAW** data. The spooler passes the bytes to the device untouched (no GDI
rasterization, no driver page rendering).

Win32 sequence (all in `winspool.h`, linked with `-lwinspool`):

```
OpenPrinterW(name, &hPrinter, NULL)
StartDocPrinterW(hPrinter, 1, {docName, NULL, L"RAW"})   // datatype = RAW
StartPagePrinter(hPrinter)
WritePrinter(hPrinter, escposBytes, len, &written)        // can be called repeatedly
EndPagePrinter(hPrinter)
EndDocPrinter(hPrinter)
ClosePrinter(hPrinter)
```

- **Dependencies**: none beyond the OS. `winspool` ships with Windows; MinGW has the headers
  and import lib. No Excel, no QXlsx, no COM, no Epson DLL, no `cscript`.
- **Printer discovery**: uses the installed queue name (e.g. `EPSON TM-T20III Receipt`) or the
  user's default printer (`GetDefaultPrinter`). Works for USB and Ethernet queues identically.
- **Speed**: immediate — no Excel process launch.
- **Status feedback**: limited to spooler job state by default; can be upgraded (option B or
  reading the device with `GetPrinter`/bidirectional port) later.

### B. Epson Status API (`EPSStmApi.dll`, `BiDirectIOEx`)

Epson's C DLL (shipped in `APD6_StatusAPI_I_WM`) that both **sends ESC/POS** and **reads
status**. Same ESC/POS payload as option A, different transport + a status channel.

Key functions (full list in [`03_escpos_command_reference.md`](03_escpos_command_reference.md#status-api-functions)):

```
int  BiOpenMonPrinter(int nType, LPSTR pName);   // nType 1=port, 2=printer name; returns handle
int  BiDirectIOEx(handle, writeLen, writeCmd, &readLen, readBuff, timeout, nullTerminate, option);
int  BiGetStatus(handle, &status);               // ASB: online/offline, cover, paper end, error
int  BiOpenDrawer(...);                           // cash drawer kick
int  BiCloseMonPrinter(handle);
```

- **Dependencies**: requires the APD + Status API to be installed on the machine, and bundling
  `EPSStmApi.h` + linking `EPSStmApi.lib`/loading the DLL. Heavier, Epson-specific.
- **Upside**: real-time paper-out / cover-open / cutter-error detection (ASB), drawer control,
  printer reset. The current flow has none of this.
- **Best use**: an **optional status layer** on top of A, or the transport if we want guaranteed
  status. Not required for the core "stop using Excel" goal.

### C. APD raster driver via GDI device fonts (what the C++ samples do)

Print like a normal Windows document: `CreateDC(NULL, "EPSON TM-T20III Receipt", ...)`,
`StartDoc`, select an APD **device font** (`FontA11`, `FontB9`, …), `TextOut`, `EndPage`,
`EndDoc`. The APD driver converts the page to ESC/POS internally.

- This is essentially the model we are **moving away from** — it renders a "page" through the
  driver rather than talking the printer's language. It still depends on the APD driver and
  GDI, and gives less precise control over receipt formatting than raw ESC/POS.
- Useful only as a reference for how the bundled samples work. Not recommended.

### D. OPOS ADK (UnifiedPOS, OCX/COM)

The retail-standard OLE driver (`OPOSADK/`). Register the device with SetupPOS, then call
`PrintNormal`, `CutPaper`, `OpenDrawer`, `DirectIO` through COM.

- Designed for VB/VS/.NET. Usable from C++ only via COM automation — exactly the kind of
  fragile COM dependency (cf. the current Excel COM) we want to eliminate. Adds SetupPOS
  registration as a deployment step. Not recommended here.

### E. ePOS-Print XML (HTTP, XML)

POST an XML document describing the receipt to the printer's built-in HTTP server.

- **Ethernet model only.** If the shop's unit is USB+Serial (most likely), this is simply not
  available. Even on Ethernet it adds an HTTP/XML stack for no benefit over ESC/POS here. Not
  recommended.

## Comparison matrix

| Criterion | A. RAW ESC/POS | B. Status API | C. APD GDI | D. OPOS | E. ePOS XML |
|-----------|:--------------:|:-------------:|:----------:|:-------:|:-----------:|
| Removes Excel/QXlsx/cscript | ✅ | ✅ | ✅ | ✅ | ✅ |
| Extra runtime dependency | none (`winspool`) | Epson DLL + APD | APD driver | OPOS/COM | none (HTTP) |
| Works on USB variant | ✅ | ✅ | ✅ | ✅ | ❌ (Eth only) |
| Fits Qt/C++ + MinGW cleanly | ✅ | ✅ (C API) | ⚠️ GDI | ❌ COM | ⚠️ HTTP/XML |
| Precise receipt control | ✅ full | ✅ full | ⚠️ via driver | ⚠️ via API | ⚠️ via XML |
| Real-time paper/cover status | ⚠️ limited | ✅ ASB | ❌ | ✅ | ⚠️ |
| Speed vs current | ✅ instant | ✅ instant | ✅ | ✅ | ✅ |
| Deployment friction | lowest | install APD | install APD | SetupPOS reg | network setup |

## Decision

**Primary: Option A — direct ESC/POS through the RAW spooler.** It is the lowest-dependency,
most portable, fully-controllable path; it removes every piece of the current Excel pipeline;
and it keeps working against the printer queue the shop already has installed, USB or Ethernet.

**Optional enhancement: Option B — Status API**, added later behind a settings flag, purely to
gain paper-out / cover-open detection and (if ever wanted) cash-drawer control. The ESC/POS
payload built for A is reused verbatim — only the transport changes — so adopting B later is
incremental, not a rewrite.

Everything else (C/D/E) is documented for completeness and rejected for our context.

## Why RAW over "just keep using the driver"

The current flow already prints through the driver (via Excel). The problems it has —
Excel must be installed and licensed, COM automation is slow and brittle, a `.vbs` is written
to disk and run with `cscript`, there is zero error/status feedback, and layout is fought
through spreadsheet cells — are all consequences of treating a 48-column receipt as a
spreadsheet document. Talking ESC/POS directly is the printer's native language: a few hundred
bytes, sent in milliseconds, laid out exactly for the column width.

## References

- `TM-T20III_trg_en_revD.pdf` — "Application Development Information → Controlling the Printer"
  (lists ESC/POS, ePOS-Print XML, APD, OPOS, OPOS for .NET, ePOS SDK as the control options).
- `APD6_Status_en_revD.pdf` — Status API Win32 reference (`BiOpenMonPrinter`, `BiDirectIOEx`,
  `BiGetStatus`, `BiOpenDrawer`, `BiCloseMonPrinter`, error codes).
- `APD_SampleProgram_EN_D/TM/Sample/Src/C++/…` — the GDI device-font sample (option C).
- Microsoft Win32 Print Spooler API docs (`OpenPrinter`, `StartDocPrinter`, `WritePrinter`).
- mike42.me "What is ESC/POS, and how do I use it?" — practical raw-printing walk-through.
