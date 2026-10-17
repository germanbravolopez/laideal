# Automated UI smoke test

Drives the **installed** `laideal.exe` the way a person does (mouse and keyboard over Windows UI Automation) on a disposable Windows VM, so the pre-release checklist in [smoke_test.md](smoke_test.md) runs by itself: installer, deployed DLLs, menus, windows, printing, Contabilidad and the quarter lock. It complements `ctest`: the e2e bench ([e2e.md](e2e.md)) calls the windows' slots from inside a test exe, while this test only sees what is on screen, what is printed and what lands in the database.

Two scripts share one set of operator flows:

| Script | Where | What |
|--------|-------|------|
| `tests/ui/smoke.py` | CI (`ui-smoke.yml`), or any PC | Smoke test blocks A-E against a **fake AEAT**, on a synthetic database |
| `tests/ui/aeat_preproduction.py` | **Locally**, on a PC with the issuer's certificate | The `laideal-prueba-aeat` kit's flows against **AEAT pre-production**, with a report |

---

## The UI-test build and the fake AEAT

The paid flows need Verifactu, and CI has neither a certificate nor AEAT. The CMake option **`LAIDEAL_UI_TEST`** (OFF by default) builds `laideal.exe` with a `--fake-aeat http://127.0.0.1:<port>` switch that points the direct AEAT client at a local fake AEAT. Anything other than `127.0.0.1` is refused. The same build never opens generated PDFs in a viewer, and its title says "AEAT simulada (prueba de interfaz)".

It must never reach a shop, because it could mark invoices as sent without AEAT (the "no dual-use" requirement):

- The release job in `ci.yml` **refuses an exe containing the `--fake-aeat` marker**. `ui-smoke.yml` checks that its own build does contain the marker, so the guard cannot go blind.
- `release.ps1` configures with `-DLAIDEAL_UI_TEST=OFF` and refuses the marker too; configuring the option ON prints a CMake warning (keep it in a separate `build-uitest` folder, since the option is cached).
- `ui-smoke.yml` uploads no exe or installer.

`tests/tools/fake_aeat_server.cpp` runs the e2e suites' `FakeAeatServer` (`tests/support/fakeaeatserver`) as its own process. It writes its URL to a file and keeps a JSON copy of the records it registered.

## Where it runs

`.github/workflows/ui-smoke.yml` runs on a GitHub-hosted `windows-latest` runner: a fresh VM with a desktop session for every run. Docker is out because Windows containers have no desktop and no printer spooler. Windows Sandbox and Hyper-V are out because they need Pro, and this PC runs Home.

Triggers: `workflow_dispatch` (run it before every release, `/release` step 1), and pushes to `develop` / `feature/**` that touch `tests/ui/`, `tests/tools/` or the workflow.

Steps:
1. Build the UI-test app and the two tools.
2. Check the marker.
3. Package with `windeployqt` + Inno Setup as the release job does, and deploy the tools with their own Qt DLLs.
4. Install with `/VERYSILENT /LANG=spanish`.
5. Add the capturing printer queue.
6. Run `smoke.py` with the build's Qt removed from `PATH`, so the app starts on the DLLs that were installed with it.
7. Run the pre-production script in its dry run.

About 9 minutes in all, of which the UI run takes about 4 and the pre-production dry run 1.

The artifact **`ui-smoke`** keeps:
- `report.md`, which is also the job summary
- a screenshot per step (`steps/`)
- the control tree of every open window for a failed step (`failures/<step>/`)
- the captured print jobs, decoded (`print-jobs/`)
- the reports and PDFs
- the database
- the app's log for the run (`laideal-run.log`) and its output
- the fake AEAT's records
- the installer log

## Safety

- **Never shop data**: the repository is public. `tests/ui/seed.py` builds a throwaway database with the shop's original tables (as `tests/support/testschema.h`). The first start runs the app's own migrations. Then the tickets and gasto of smoke_test.md step 1.6 are seeded, plus a few garments, clients and a supplier.
- **Own settings**: the app starts with `--settings <file>`, so `~/.laideal_settings.json` is never touched. The file sets a synthetic issuer (B00000000) on the direct client in the test environment, and turns off the update check and the automatic backup.
- **The log**: Qt reads the profile folder from the Windows account, so the app always writes `~/.laideal.log`. The scripts read only what each session added, and copy it to `laideal-run.log`. A local run appends to the developer's log; CI's is the runner's.

## What it checks

| Block | Steps (smoke_test.md) | How |
|-------|-----------------------|-----|
| A | A1 first start: migrations in the log, no SQL error, verifactu columns added, status bar "entorno de PRUEBAS"; A2 restart after seeding: schema unchanged, no data migration repeats, the recovery dialog lists ticket 3 only; A3 every AEAT URL in the log is the fake's, never AEAT's or the gateway's (also at the end of the run) | DB schema, log, the dialog's table |
| B | B1 Posponer writes nothing and the dialog comes back; B2 Marcar como error → `ERROR`, Recogida's Verifactu window offers Reintentar; B3 ticket 1 by number, Importe total 20,00; B4 Añadir prendas… adds an unpaid Falda; B5 Cobrar… charges only the Camisa, the fake AEAT accepts it (`ENVIADA`, CSV); B6 Añadir nuevas prendas refuses ticket 1; B6b Anular prendas… voids ticket 2, and Herramientas has no Anular prendas; B7 the four date columns side by side (on-screen order); B8 `SIN COBRAR` shown, accent-insensitive client search, Listado de prendas PDF; B9 Anular factura / Rectificar load ticket 4, nothing sent | DB rows, result panels, no pop-up opened |
| C | C1 unpaid ticket: recibo with both copies, no QR, number advances; C2 paid ticket: factura with QR + leyenda, recibo with IMPORTE PAGADO, QR decoded to AEAT's test check page for that number, ticket `ENVIADA`; C3 Imprimir → Factura of ticket 4 (total, QR), Factura completa with the typed DNI and address | Captured print jobs decoded by `escpos_preview` |
| D | D1 Q3 2026 with detail: income 66.30, gastos 60.50, the window stays open; D2 Q4: the cancellation of ticket 5 is taken back (-12.10); D3 Anual renders; D4 the lock box only in Trimestral (never in Revertir), Q3 without detail, Comprobar bloqueo "no está bloqueado"; D5 Bloquear Q3: SMOKE-G1 not editable, an expense dated 20-09-2026 refused, today's saved; D6 Revertir Q3: SMOKE-G1 editable again | PDF text (`pypdf`), DB, result panels, the focused cell editor |
| E | E1 backup next to the database; E2 Configuración has no IVA field; E3 English: Yes/No buttons and English release notes, back to Español: Sí/No; E4 the installer's language becomes the app's first language (English, then Spanish installer) | DB files, dialogs, the settings file |

Blocks run in this order on one database, as a person would. A failed step stops its block, and the run goes on with the next one. A block run alone (`--blocks D`) first does the start-up and seeding of block A. D2 needs block B's payment in Q4 and is skipped without it (see the Contabilidad issue in the tracker).

**Stays manual** (smoke_test.md): the physical printer and paper, how the screens and PDFs look (reviewed from the screenshots), and the installer's language pages.

## Printing (block C)

`tests/ui/printer.py install` adds a queue `LaIdeal-Smoke` with the built-in *Generic / Text Only* driver on a Standard TCP/IP port (RAW) to `127.0.0.1:9100`. While block C runs, a listener there saves each job as `print-jobs/job-NNN.prn`. A FILE: port would overwrite its file on every job and lose the second copy of a ticket.

`laideal.exe` sends RAW ESC/POS through `WritePrinter`, so the bytes arrive unchanged. `escpos_preview.exe` (`tests/tools/`) decodes them with the same reader as `test_ticket_preview` (`tests/support/escpospreview.h`) into an ASCII mock (`.txt`, with `[QR wxh]` for the QR image) and a PNG of the paper. OpenCV then reads the QR from the PNG; where OpenCV has no wheel (Python 3.15), that one check is left out.

Adding the queue needed no administrator rights on this PC. `printer.py remove` deletes it.

## Finding widgets

Qt 6 publishes a widget's `objectName` as its UIA **AutomationId**, so the scripts use the names the e2e bench uses with `findChild` (`pb_save`, `lblResult`, `tableView`, `btnCobrar`…). Menu actions carry their window's prefix (`MainWindow.actionGastos`), so `find` matches the last segment. What the spike and the blocks found:

- **Dialogs are nested** `Window` elements inside the window that opened them, mostly without an object name, so they are found by title or by a widget they contain. Lookups stop at nested windows: `find(main, "lblResult")` is MainWindow's panel even while a dialog with its own `lblResult` is open. The recovery dialog is window-modal, so each block starts by closing it (`fresh_start`).
- **Widgets placed in table cells** (`setCellWidget`) are not in the tree: MainWindow's garment / service combos, the recovery dialog's Reintentar / Error / Posponer buttons, Anular factura's row button. They are clicked inside their cell (`click_in_cell`).
- **Checkable cells** (Cobrar, Anular prendas) have no UIA toggle pattern. Their starting state is known (all ticked / all unticked), the check box is clicked (`click_check`), and the database proves the result.
- **Hidden columns** keep a zero-width header but have no cells, so `headers()` leaves them out. The tree lists columns in model order, so `visual_headers()` sorts by position for what the screen shows. Wide listados (ingresos) do not line up headers with cells, so checks there look for a cell's text.
- **Date fields** take typing section by section only (`set_date`: Shift+Right selects a section).
- **A table's cell editor** is not in the table's tree, but it has the keyboard focus (`focused_control_type`).

## The AEAT pre-production run (local)

`aeat_preproduction.py` is the automated form of the `laideal-prueba-aeat` kit. Run it on a PC with the issuer's certificate and a build of this branch:

```bash
.venv/Scripts/python tests/ui/aeat_preproduction.py --exe "<folder>/laideal.exe" \
    --settings-from "<kit>/prueba-aeat-settings.json" --out aeat-run
```

- **What it takes from the kit's settings**: only the issuer's NIF, name and certificate (store thumbprint, or `.pfx` file + password, DPAPI-encrypted for the same Windows user).
- **What it forces**: a new synthetic database, the direct client, the test environment, PRODUCCIÓN off.
- **Safety stop**: the script kills the app unless the status bar says *PRUEBAS* and every AEAT URL in the log starts with `https://prewww1.aeat.es/`. It checks once at the start and once at the end.
- **Ticket numbers**: they start at `9DDDhhmm + 1` (day of the year, time). That is eight digits like the kit's `90000001`, because Recogida reads a nine-digit search as a phone number. They stay above the shop's numbers and those of earlier runs.

The steps:
1. Safety check, and the first chain sync.
2. A paid ticket.
3. A ticket paid in two payments through Recogida.
4. The cancellation of its first payment.
5. R5 rectifications of the first ticket, by differences and by substitution.
6. Exportar registros AEAT of the day.
7. The log check again.

`--out` must be a new or empty folder. On the shop PC the run's lines go to that PC's `~/.laideal.log` (marked PRUEBAS); prefer another PC with the certificate.

Each AEAT answer is awaited for up to 4 minutes, because AEAT asks for up to 60 s between submissions. `report.md` ends with every invoice's estado, CSV and AEAT error.

It stays local because the certificate must not become a secret of a public repository. `--dry-run --tools <folder>` runs the same script against the fake AEAT with the UI-test build. CI runs it that way on every ui-smoke run, so the script is known to work before it is pointed at AEAT.

## Running it locally

Use a UI-test build, and run from the repository root inside a virtual environment in `.venv/` (git-ignored), not the global Python:

```powershell
cmake -B build-uitest -G Ninja -DCMAKE_BUILD_TYPE=Release -DLAIDEAL_UI_TEST=ON
cmake --build build-uitest --target laideal fake_aeat_server escpos_preview
```

```bash
python -m venv .venv
.venv/Scripts/python -m pip install -r tests/ui/requirements.txt
.venv/Scripts/python tests/ui/printer.py install          # once, for block C
.venv/Scripts/python tests/ui/smoke.py --exe build-uitest/src/app/laideal.exe \
    --tools build-uitest/tests --out ui-smoke-out --printer LaIdeal-Smoke
```

- **PATH**: a build tree needs Qt's `bin` on `PATH` (README §Build).
- **PowerShell**: the interpreter is `.venv\Scripts\python.exe`.
- **Partial runs**: `--blocks AB` runs only some blocks.
- **Installer step**: `--installer <setup.exe>` adds E4, which reinstalls in place; use it on a disposable machine only.
- **During a run**: it takes the mouse and keyboard for about 3-4 minutes, so do not touch them, and keep the screen unlocked (a locked screen blocks input and screenshots).

## Files

| File | Role |
|------|------|
| `tests/ui/smoke.py` | Runner: fake AEAT, settings, blocks, steps with screenshots / failure trees, `report.md` |
| `tests/ui/block_a.py` … `block_e.py` | The smoke blocks |
| `tests/ui/actions.py` | Operator flows: save a ticket, Recogida search / Cobrar / Anular prendas, cancel / rectify, print, Contabilidad, gastos, export |
| `tests/ui/laideal_ui.py` | pywinauto helpers: start the app, windows and nested dialogs, scoped `find`, message boxes, menus, cell clicks, dates |
| `tests/ui/seed.py` | Throwaway database, smoke tickets, settings file |
| `tests/ui/printer.py` | Capturing printer queue: install / remove, job listener, decode |
| `tests/ui/aeat_preproduction.py` | The local AEAT pre-production run (and its dry run) |
| `tests/ui/requirements.txt` | pywinauto, Pillow, pypdf, OpenCV (QR) |
| `tests/tools/fake_aeat_server.cpp`, `tests/tools/escpos_preview.cpp` | The fake AEAT as a process; the ESC/POS job decoder |
| `tests/support/escpospreview.h` | ESC/POS reader shared with `test_ticket_preview` |
| `.github/workflows/ui-smoke.yml` | The CI run |
