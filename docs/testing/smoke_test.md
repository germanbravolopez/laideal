# Smoke test — manual pre-release checklist

What still needs a person before a release, about 15 minutes. Everything else is automated:

- `ctest`: the unit suites and the end-to-end bench ([unit_suites.md](unit_suites.md), [e2e.md](e2e.md)).
- The **UI smoke test** on a disposable runner ([ui_smoke.md](ui_smoke.md), `ui-smoke.yml`): the installed app driven like a person, from the first start and its migrations, through Recogida, Cobrar, printing, Contabilidad and the quarter lock, to backup, language and the installer's language.
- The **AEAT pre-production run** as a local script ([ui_smoke.md](ui_smoke.md#the-aeat-pre-production-run-local)).

Run this after `ui-smoke.yml` is green on the release branch (`/release` step 1). What is left by hand: the shop's real schema and data, the physical printer and paper, how screens and PDFs look, and the installer's pages.

---

## Safety

- **Never the live database.** Work on a copy of the newest backup.
- **Never the shop's settings.** Start the app with a copy of them (`--settings`), so `~/.laideal_settings.json` is not touched.
- **No invoice is sent.** Nothing below saves a paid ticket. The copy runs on the direct client in **AEAT pre-production**, so the start-up chain check reads AEAT's test environment only. Never use the gateway here: its test and production URLs are the same IreneSolutions URL.

## Setup

1. Copy the newest backup and the settings:
   ```powershell
   Copy-Item "<db folder>/backups/laideal_<newest>.db" "$env:USERPROFILE\Desktop\laideal_SMOKE.db"
   Copy-Item "$env:USERPROFILE\.laideal_settings.json" "$env:USERPROFILE\Desktop\smoke-settings.json"
   ```
2. In `smoke-settings.json` (a text editor) set:
   - `database.path`: the full path of `laideal_SMOKE.db` (forward slashes);
   - `verifactu.connection`: `"aeat"`, `verifactu.aeat_direct_production`: `false`, and `verifactu.service_key`: `""`.
3. Start the **release build** on them:
   ```powershell
   & "<release folder>\laideal.exe" --settings "$env:USERPROFILE\Desktop\smoke-settings.json"
   ```
   The title says "configuración de pruebas". The status bar **must** say *"AEAT: entorno de PRUEBAS"*; if it does not, close the app and stop.

## Checks

| # | Area | Step | Expected |
|---|------|------|----------|
| M1 | Real schema | The first start on the copy | Starts; the log (Ayuda → Log de depuración) has the `migrateDatabase` lines and no SQL error; the next ticket number is the last real one + 1 |
| M2 | Real data | Recogida: search a recent ticket by number and a client by name; Contabilidad: the last closed quarter, without Bloquear | The ticket and its garments show as in the shop; the report's totals match the one filed for that quarter |
| M3 | Printer | Printer connected, printing on: save an **unpaid** ticket; Imprimir → Recibo and Factura of a recent **paid** ticket | Recibo: both copies, cut, accents legible. Factura: header, lines, IVA, legal text and the QR, which a phone scans and opens on AEAT's page |
| M4 | Look | The `ui-smoke` artifact: `steps/*.png` and the PDFs under `reports/` | Texts, buttons and groups legible and aligned, nothing cut off; the Contabilidad `_detalle` PDF and the listado PDF render |
| M5 | Installer | Start the release installer interactively, once in Español and once in English; cancel before installing | The information page shows that language's release notes with correct accents |
| M6 | AEAT (only when the release changes the direct AEAT client) | The pre-production script with the certificate ([how](ui_smoke.md#the-aeat-pre-production-run-local)) | Every step passes; keep its `report.md` |

## Teardown

Close the app, then delete `laideal_SMOKE.db` and `smoke-settings.json` from the Desktop. Both hold shop data.

The one real-AEAT check that stays out of every test is a single real ticket right after the production switch (see that row in `docs/progress_tracker.md`).
