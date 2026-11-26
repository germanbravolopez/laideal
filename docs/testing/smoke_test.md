# Smoke test - what still needs a person

Everything else is automated:

- `ctest`: the unit suites and the end-to-end bench ([unit_suites.md](unit_suites.md), [e2e.md](e2e.md)).
- The UI smoke test on a disposable runner ([ui_smoke.md](ui_smoke.md)): the installed app driven like a person, from the first start through Recogida, Cobrar, printing, Contabilidad, the quarter lock, backup, language and the installer's language.
- The AEAT pre-production run as a local script ([ui_smoke.md](ui_smoke.md#aeat-pre-production-run)).

What is left needs eyes before the release (part 1), or the shop's real data and printer after it is installed (part 2, the self-check the shop user runs).

---

## 1. Before the release

You, after the `ui-smoke` job is green on the release PR (`/release` steps 1 and 5).

| # | Area | Step | Expected |
|---|------|------|----------|
| M4 | Look | The `ui-smoke` artifact: `steps/*.png` and the PDFs under `reports/`; Configuración, Verifactu tab | Texts, buttons and groups legible and aligned, nothing cut off; the Contabilidad `_detalle` PDF and the listado PDF render; the tab shows only the chosen connection's fields (the direct one with *Probar conexión* and *Ensayo en el entorno de pruebas de la AEAT*) |
| M6 | AEAT (only when the release changes the direct AEAT client) | The pre-production script with the certificate ([how](ui_smoke.md#aeat-pre-production-run)) | Every step passes; keep its `report.md` |

On a PC with a copy of the shop's data you can also run part 2 before publishing, with the new build: `laideal.exe --self-check`.

## 2. After installing at the shop: *Comprobar tras actualizar*

The shop user runs `laideal.exe --self-check` (`src/app/selfcheck.cpp`, `selfcheckdialog.cpp`) once after installing, answers its questions and sends back what its last screen lists. It is reached four ways:

- the installer's last page, **Comprobar la instalación (recomendado tras actualizar)**, ticked by default;
- **Start, La Ideal - Comprobar tras actualizar**;
- **Ayuda, Comprobar tras actualizar...** in the app, any time (its own process, same settings file);
- the first start of a new version offers it once in the result panel (`app.last_started_version`; never on a first installation).

Its last screen lists what to send: the report saved on the Desktop (`comprobacion_laideal_<version>_<date>.txt`), the app's log (`~/.laideal.log`, linked) and a photo of the first paid factura printed with the new version (QR and leyenda).

**Safety**

- It works on a copy of the newest backup (`<db folder>/backups/laideal_*.db`) with a copy of the settings, both in a temporary folder and deleted at the end. With no backup it stops at check M0 ("Copia de los datos") and asks to open the program once.
- `~/.laideal_settings.json` and the live database are only read; `test_selfcheck` checks this byte for byte.
- The Verifactu client is never started, so nothing reaches AEAT or the gateway. The copied settings also put the direct client in pre-production with no gateway key and backups off.
- The factura's QR is drawn from the verification URL stored with the invoice.

**Checks**, in the order the window runs them

| # | Check | Kind | Correct when |
|---|-------|------|--------------|
| M1a | Settings health | Automatic | The settings file is readable JSON and its database path exists |
| M1b | Backup age | Automatic | The newest backup is at most 3 days old (`BackupManager::backupWarning`) |
| M1 | Real schema and numbering | Automatic | The migrations run on the copy with no failed statement; `PRAGMA integrity_check` is `ok`; `ingresos` has every column and `accounting_closures` exists; the next ticket number is the highest stored + 1 |
| M1d | Migrations run twice | Automatic | A second `migrateDatabase` changes no schema object or row (SHA-256 of the copy) and logs no failed statement |
| M1e | Money sanity | Automatic | No `ingresos` row with an unreadable importe and no gasto with an unrecognised IVA (the reason gives counts, and ticket numbers for the first) |
| M1c | AEAT record chain | Automatic | Every stored `aeat_records` record of both environments matches its huella and links to the previous one (`AeatChainCheck`); the reason names only the record's position |
| M2 | Real data | Automatic | The newest ticket loads with its garments and a search by its client finds it |
| M2b | Last closed quarter | Asked | The quarter's income, IVA, gastos and IVA settlement are recomputed without closing anything; the user confirms they match the filed report (*Abrir el informe presentado* opens its PDF when it is in the Contabilidad folder) |
| M3 | Printer and QR | Asked | Prints the recibo and the factura of the newest ticket AEAT holds (still sent in the live DB); both copies printed and cut, accents legible, and a phone scanning the QR opens AEAT's page. Skipped when printing is off in Configuración or no invoice has been sent |
| M5 | Installer | Asked | The installer's information page showed the release notes in the chosen language with correct accents |

The report lists each check as CORRECTO, FALLO or OMITIDO with a reason free of shop data (no names, numbers or amounts) and the user's note. A FALLO is investigated with `/debugging` from the report and the shop's log (`~/.laideal.log`, Ayuda, Log de depuración). The shop keeps working with the release unless the failure blocks it; then reinstall the previous release from GitHub.

The one real-AEAT check outside every test is a single real ticket right after the production switch (see `docs/progress_tracker.md`).
