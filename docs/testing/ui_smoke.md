# Automated UI smoke test

Drives the installed `laideal.exe` like a person (mouse and keyboard over Windows UI Automation, pywinauto): installer, deployed DLLs, menus, windows, printing, Contabilidad and the quarter lock. The e2e bench ([e2e.md](e2e.md)) calls the windows' slots from inside a test exe; this test sees only what is on screen, what is printed and what lands in the database. What still needs a person is in [smoke_test.md](smoke_test.md).

| Script | Where | What |
|--------|-------|------|
| `tests/ui/smoke.py` | CI (`ui-smoke` job), or any PC | Blocks A-E against a **fake AEAT**, on a synthetic database |
| `tests/ui/aeat_preproduction.py` | **Locally**, on a PC with the issuer's certificate | The `laideal-prueba-aeat` kit's flows against **AEAT pre-production**, with a report |

**Both scripts need an unlocked, interactive desktop.** They move the mouse, type and take screenshots. On a locked session they fail with "There is no active desktop". Do not touch the mouse or keyboard while one runs.

---

## The UI-test build and the fake AEAT

The paid flows need Verifactu, and CI has neither a certificate nor AEAT. The CMake option `LAIDEAL_UI_TEST` (OFF by default) builds `laideal.exe` with a `--fake-aeat http://127.0.0.1:<port>` switch that points the direct AEAT client at a local fake; any other host is refused. This build never opens generated PDFs in a viewer, and its title says "AEAT simulada (prueba de interfaz)".

It must never reach a shop (it could mark invoices as sent without AEAT), so:

- `releases/package.ps1` refuses an exe containing the `--fake-aeat` marker. The `ui-smoke` job checks its own build does contain it, so the guard cannot go blind.
- `release.ps1` configures with `-DLAIDEAL_UI_TEST=OFF` and refuses the marker too. Configuring the option ON prints a CMake warning; keep it in a separate `build-uitest` folder, since the option is cached.
- The `ui-smoke` job uploads no exe or installer.

`tests/tools/fake_aeat_server.cpp` runs the e2e suites' `FakeAeatServer` as its own process: `--url-file <file>` receives its URL, `--records-file <file>` a JSON copy of the records it registered ([e2e.md](e2e.md)).

## Where it runs

The `ui-smoke` job of `.github/workflows/ci.yml` runs on a GitHub-hosted `windows-latest` runner, a fresh VM with a desktop session. Docker (no desktop, no spooler) and Windows Sandbox / Hyper-V (need Pro) are not options.

**Triggers**: every pull request and `workflow_dispatch` (`gh workflow run ci.yml --ref <branch>`; run it before every release, `/release` step 1). `master` requires it to pass.

**Steps** (about 9 minutes, of which the UI run takes about 4 and the dry run 1):
1. `make uitest`: the UI-test app and the two tools.
2. Check the marker.
3. Package with `windeployqt` and Inno Setup as the release job does; deploy the tools with their Qt DLLs.
4. Install silently (`/VERYSILENT /LANG=spanish`).
5. `make venv`, `make printer`.
6. `make ui-smoke -Exe ... -Tools ... -Printer ... -Installer ...` with the build's Qt off `PATH`, so the app starts on the DLLs installed with it.
7. `make aeat-dry` against the installed app.

**The `ui-smoke` artifact** keeps `report.md` (also the job summary), a screenshot per step (`steps/`), the control tree of every window for a failed step (`failures/<step>/`), the captured print jobs decoded (`print-jobs/`), the reports and PDFs, the database, the app's log (`laideal-run.log`) and the fake AEAT's records, and the installer log.

## Safety

- **Never shop data** (the repository is public). `tests/ui/seed.py` builds a throwaway database with the shop's original tables; the first start runs the app's migrations; then the smoke tickets are seeded (`seed.SMOKE_TICKETS`): **1** unpaid with two garments (Camisa 12.00, Pantalon 8.00), **2** unpaid (Falda), **3** paid and left `PENDIENTE`, **4** paid and `ENVIADA` (24.20), **5** paid in Q3 and `ANULADA` on 01-10-2026 (12.10), gasto **SMOKE-G1** (60.50, Q3), plus a few garments, clients and a supplier.
- **Own settings**: the app starts with `--settings <file>`, so `~/.laideal_settings.json` is never touched. The file sets a synthetic issuer (B00000000) on the direct client in the test environment and turns off the update check and the automatic backup.
- **The log**: the app always writes `~/.laideal.log`. The scripts read only what their session added and copy it to `laideal-run.log`. A local run appends to the developer's log.

## What it checks

| Block | Steps | Evidence |
|-------|-------|----------|
| A | A1 first start: migrations in the log, no SQL error, verifactu columns added, status bar "entorno de PRUEBAS"; A2 restart after seeding: schema unchanged, no data migration repeats, the recovery dialog lists ticket 3 only; A3 every AEAT URL in the log is the fake's | DB schema, log, the dialog's table |
| B | B1 Posponer writes nothing; B2 Marcar como error gives `ERROR` and Recogida offers Reintentar; B3 ticket 1 by number, Importe total 20,00; B4 Añadir prendas adds an unpaid Falda; B5 Cobrar charges only the Camisa, accepted (`ENVIADA`, CSV); B6 Añadir nuevas prendas refuses ticket 1; B6b Anular prendas voids ticket 2; B7 the four date columns side by side; B8 `SIN COBRAR` shown, accent-insensitive client search, Listado de prendas PDF; B9 Anular factura / Rectificar load ticket 4, nothing sent | DB rows, result panels, no pop-up |
| C | C1 unpaid ticket: recibo with both copies, no QR, number advances; C2 paid ticket: factura with QR and leyenda, recibo with IMPORTE PAGADO, the QR decoding to AEAT's test check page (read by OpenCV; its symbol changes with the day, so a resized read counts, with a warning in the output), ticket `ENVIADA`; C3 Imprimir of ticket 4 (factura, factura completa with DNI and address), each with its QR read at the printed size and equal to the seeded text (a fixed symbol) | Captured print jobs decoded by `escpos_preview` |
| D | D1 Q3 2026 with detail: income 66.30, gastos 60.50; D2 Q4 takes back ticket 5's cancellation (-12.10); D3 Anual renders; D4 the lock box only in Trimestral, Comprobar bloqueo "no está bloqueado"; D5 Bloquear Q3: SMOKE-G1 not editable, an expense dated 20-09-2026 refused, today's saved; D6 Revertir Q3: editable again | PDF text (`pypdf`), DB, result panels, the focused cell editor |
| E | E1 backup next to the database; E2 Configuración has no IVA field; E3 English (Yes/No buttons, release notes) and back to Español; E4 the installer's language becomes the app's first language (needs `--installer`) | DB files, dialogs |

Blocks run in order on one database. A failed step stops its block and the run goes on. A block run alone (`--blocks D`) first does block A's start-up and seeding.

## Printing (block C)

`tests/ui/printer.py install` adds a queue `LaIdeal-Smoke` (built-in *Generic / Text Only* driver) on a Standard TCP/IP port, RAW, `127.0.0.1:9100` (it asks for an administrator; `remove` deletes it). While block C runs, a listener there saves each job as `print-jobs/job-NNN.prn`. A FILE: port would overwrite its file on every job and lose a ticket's second copy.

`laideal.exe` sends RAW ESC/POS through `WritePrinter`, so the bytes arrive unchanged. `tests/tools/escpos_preview.cpp` decodes them with the reader of `test_ticket_preview` (`tests/support/escpospreview.h`) into an ASCII mock (`.txt`, `[QR wxh]` marks the QR) and a PNG of the paper. OpenCV reads the QR from the PNG; where OpenCV has no wheel, that one check is left out.

To see what a manual run prints without the shop's printer: install the queue, start the listener in its own PowerShell window, and enable printing on `LaIdeal-Smoke` (Configuración, Impresión, or `print.enable` `"Yes"` and `print.printer_name` `"LaIdeal-Smoke"` in the settings file).

```powershell
.\.venv\Scripts\python.exe .\tests\ui\printer.py install            # once
.\.venv\Scripts\python.exe .\tests\ui\printer.py listen --tools .\build-uitest\tests --out print-jobs
```

## Finding widgets

Qt 6 publishes a widget's `objectName` as its UIA AutomationId, so the scripts use the names the e2e bench uses (`pb_save`, `lblResult`, `btnCobrar`...). Menu actions carry their window's prefix (`MainWindow.actionGastos`), so `find` matches the last segment.

- **Dialogs** are nested `Window` elements, mostly without an object name: found by title or by a widget they contain. Lookups stop at nested windows, so `find(main, "lblResult")` is MainWindow's panel even while a dialog is open. The window-modal recovery dialog is closed at the start of each block (`fresh_start`).
- **Widgets in table cells** (`setCellWidget`: garment and service combos, the recovery dialog's buttons, Anular factura's row button) are not in the tree; they are clicked inside their cell (`click_in_cell`).
- **Checkable cells** (Cobrar, Anular prendas) have no toggle pattern: the starting state is known, the box is clicked (`click_check`) and the database proves the result.
- **Hidden columns** keep a zero-width header but have no cells, so `headers()` leaves them out; `visual_headers()` sorts by position for what the screen shows. Wide listados do not line up headers and cells, so checks there look for a cell's text.
- **Date fields** take typing section by section only (`set_date`).
- **A cell editor** is not in the table's tree but has the keyboard focus (`focused_control_type`).

## Running the scripts locally

Run from the repository root after the [UI-test build and the `.venv`](../../README.md#build-folders); `make ui-smoke` and the other targets do both (see [README.md](README.md#running-the-tests) for the targets and options). Keep the screen unlocked.

| | Smoke test (`smoke.py`) | AEAT pre-production run (`aeat_preproduction.py`) |
|---|---|---|
| For | Reproducing or debugging a CI failure | `/release` gate M6 when the release changes the direct AEAT client |
| AEAT | The fake AEAT (a local process) | AEAT **pre-production**, with the issuer's certificate |
| Needs | Nothing else | The certificate in the Windows store and its settings file |
| Duration | About 3-4 minutes | About 10 minutes (AEAT spaces submissions by up to 60 s) |
| Results | `ui-smoke-out\` | `aeat-run\` (git-ignored) |

### Smoke test against the fake AEAT

```powershell
.\make printer                          # once, as administrator, for block C
.\make ui-smoke -Printer LaIdeal-Smoke
```

Underneath: `printer.py install`, then `smoke.py --exe build-uitest\src\app\laideal.exe --tools build-uitest\tests --out ui-smoke-out --printer LaIdeal-Smoke`.

- `--blocks AB` runs only some blocks (`-Blocks AB` in `make`).
- Without `--printer`, block C is skipped.
- `--installer <setup.exe>` adds E4, which reinstalls in place: a disposable machine only.
- Results: `ui-smoke-out\report.md`, `steps\` screenshots, and for a failed step `failures\<step>\`.

### AEAT pre-production run

The automated form of the `laideal-prueba-aeat` kit. It stays local because the certificate must not become a secret of a public repository.

```powershell
.\make aeat-real -Settings <path to the kit's settings file>      # asks for confirmation unless -Force
.\make aeat-dry                                                     # the same against the fake AEAT
```

- **The settings file** is the kit's, kept outside the repository: it names the issuer and the certificate. The script takes only the issuer's NIF, name and certificate (store thumbprint, or `.pfx` file and password DPAPI-encrypted for the same Windows user); it works only on a PC whose store holds that certificate.
- **It forces** a new synthetic database, the direct client, the test environment and PRODUCCIÓN off.
- **Safety stop**: the script kills the app unless the status bar says *PRUEBAS* and every AEAT URL in the log starts with `https://prewww1.aeat.es/`; checked at the start and at the end.
- **Ticket numbers** start at `9DDDhhmm + 1` (day of the year, time): eight digits, because Recogida reads a nine-digit search as a phone number; above the shop's numbers and earlier runs'.
- **`--out`** must be a new or empty folder (`make` makes one per run under `aeat-run\`). The run's log lines go to the PC's `~/.laideal.log`, marked PRUEBAS; prefer another PC.
- **Waiting**: each AEAT answer is awaited up to 4 minutes; Anular and Rectificar close only once AEAT has answered.
- **Results**: `report.md` ends with every invoice's estado, CSV and AEAT error; a failed step leaves every window's control tree in `failures\<step>\`.
- **Dry run** (`--dry-run --tools <folder>`): the same script against the fake AEAT with a synthetic issuer; CI runs it on every `ui-smoke` job, so the script is known to work before it is pointed at AEAT.

The 8 steps:
1. Safety check and the first chain sync.
2. A paid ticket (`ENVIADA` with AEAT's CSV).
3. A ticket paid in two payments through Recogida (two invoices, both `ENVIADA`).
4. The cancellation of its first payment (`ANULADA`).
5. R5 rectifications: the first ticket by differences, a new paid ticket by substitution (refused on a ticket with differences).
6. Exportar registros AEAT of the day.
7. The log check again.
8. After a restart: which record's huella AEAT's query gives for the cancelled invoice. This is a finding printed under the report's table, not a verdict.

## Tools in `tests/tools/`

| Tool | Role |
|------|------|
| `fake_aeat_server.cpp` | The fake AEAT as a process (see above) |
| `escpos_preview.cpp` | Decodes captured ESC/POS jobs into `.txt` and `.png` |
| `aeat_query_dump.cpp` | Saves AEAT's raw reply to one query: `aeat_query_dump --settings <kit settings> --out <file> [--month yyyy-MM] [--invoice <number>]`. A query only (nothing is registered), always the **test** environment, run locally with the issuer's certificate. The reply holds the issuer's data, so it is redacted before it becomes a fixture in `tests/fixtures/aeat-responses` |

## Files

| File | Role |
|------|------|
| `tests/ui/smoke.py` | Runner: fake AEAT, settings, blocks, steps with screenshots and failure trees, `report.md` |
| `tests/ui/block_a.py` ... `block_e.py` | The smoke blocks |
| `tests/ui/actions.py` | Operator flows: save a ticket, Recogida, Cobrar, cancel / rectify, print, Contabilidad, gastos, export |
| `tests/ui/laideal_ui.py` | pywinauto helpers: start the app, windows and dialogs, scoped `find`, menus, cell clicks, dates |
| `tests/ui/seed.py` | Throwaway database, smoke tickets, settings file |
| `tests/ui/printer.py` | The capturing printer queue: install / remove, listener, decode |
| `tests/ui/aeat_preproduction.py` | The pre-production run and its dry run |
| `tests/ui/requirements.txt` | pywinauto, Pillow, pypdf, OpenCV |
