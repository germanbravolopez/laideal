# Automated UI smoke test

Drives the **installed** `laideal.exe` the way a person does (mouse and keyboard over Windows UI Automation) on a disposable Windows VM, so the installer, the deployed DLLs, the menus and the window wiring are checked without the hour of clicking in [smoke_test.md](smoke_test.md). It complements `ctest`: the e2e bench ([e2e.md](e2e.md)) calls the windows' slots from inside a test exe, while this test only sees what is on screen.

Status: **phase 0 (spike) done** - install, launch, find widgets, save one unpaid ticket, screenshots. Phases 1-3 (smoke test blocks, printing through a FILE-port queue, the AEAT pre-production kit) are open in [progress_tracker.md](../progress_tracker.md).

---

## Where it runs

On a GitHub-hosted `windows-latest` runner (`.github/workflows/ui-smoke.yml`): a fresh VM with a desktop session for every run. Not Docker (Windows containers have no desktop or printer spooler) and not Windows Sandbox / Hyper-V (this PC runs Windows Home).

The workflow runs on `workflow_dispatch` and on a push to `develop` / `feature/**` that changes `tests/ui/` or the workflow. Steps: build `laideal` (same Qt 6.4.3 + MinGW 11.2 as `ci.yml`), `windeployqt` + Inno Setup exactly as the release job, install with `/VERYSILENT /LANG=spanish`, then run the script with the build's Qt removed from `PATH`, so the installed app starts on its own deployed DLLs. The artifact **`ui-smoke`** keeps the screenshots, the control tree, the app output, the database, `~/.laideal.log` and the installer log.

## Safety

- **Never shop data**: the repository is public. `tests/ui/seed.py` builds a throwaway database with the shop's tables (as `tests/support/testschema.h`; the app's `migrateDatabase()` adds the `verifactu_*` columns on open) and three garments.
- **Offline settings**: the app is started with `--settings <file>` (an existing switch in `main.cpp`), so `~/.laideal_settings.json` is never read. The file has no Verifactu credentials (no AEAT call is possible), printing off, no update check, no backup, no pending-recovery dialog.

## Finding widgets

Qt 6 publishes a widget's `objectName` as its UIA **AutomationId**, plain (`pb_save`, `lblResult`, `cb_client`, `le_nr_ticket`, `table_ticket`, `le_cost_total`), so the script finds widgets by the same names the e2e bench uses with `findChild`. `laideal_ui.find(window, name)` matches the last dot segment in case a future Qt prefixes parent names. Text is read from the UIA Value pattern (edits, combos) or the Name (labels; `lblResult`'s rich text arrives as plain text).

Limits found in the spike:

- The **cell-widget combo boxes** of `table_ticket` (garment, service) are not in the UIA tree; only the cells are (`DataItem`, row by row, 6 columns). The script clicks the cell's rectangle, which lands on the combo, and types.
- Unnamed widgets (group boxes, field labels, the menu items) have an empty AutomationId; menus are reached by their visible text.

## Running it locally

With a built or installed app (a build needs Qt's `bin` on `PATH`):

```bash
python -m pip install -r tests/ui/requirements.txt
python tests/ui/smoke_p0.py --exe "build/src/app/laideal.exe" --out ui-smoke-out
```

It takes the mouse and keyboard for about 10 s; do not touch them meanwhile.

## Files

| File | Role |
|------|------|
| `tests/ui/seed.py` | Throwaway database + offline settings file; `query()` for the checks |
| `tests/ui/laideal_ui.py` | Starts the exe with `--settings`, connects pywinauto (uia), `find` / `text` / `dump_tree` / `screenshot` |
| `tests/ui/smoke_p0.py` | P0: start, read `lblResult` and the ticket number, type client + garment, Guardar, check the result panel, the stored row (`SIN COBRAR`, 3.50) and the form reset |
| `tests/ui/requirements.txt` | Pinned pywinauto + Pillow |
| `.github/workflows/ui-smoke.yml` | Build, package, silent install, run, keep the outputs |
