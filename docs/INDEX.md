# Index - La Ideal

Quick-find: use Ctrl+F for a file, class, concept or topic. `x.*` means `x.h` and `x.cpp`.

## Documents

| File | Content |
|------|---------|
| [../README.md](../README.md) | Project, requirements, build and test commands, configuration keys |
| [../CONTRIBUTING.md](../CONTRIBUTING.md) | Rules for every change, branches, release procedure, hotfix |
| [ai_agent_instructions.md](ai_agent_instructions.md) | Agent briefing: file map, doc map, skills, obligations |
| [architecture.md](architecture.md) | Modules and dependencies, data flows, database schema |
| [README.md](README.md) | Map of the docs folder |
| [progress_tracker.md](progress_tracker.md) | Current status, open issues, backlog |
| [completed_milestones.md](completed_milestones.md), [completed_milestones_archive.md](completed_milestones_archive.md) | Finished work, newest first; 10.13 and before |
| [reviews/](reviews/) | Review reports and plans, kept for reference |
| [testing/README.md](testing/README.md) | The five test layers, running them, conventions, patterns, adding a suite, CI toolchain |
| [testing/unit_suites.md](testing/unit_suites.md) | What every `tests/test_*.cpp` suite covers |
| [testing/e2e.md](testing/e2e.md) | End-to-end bench: safety rules, fake servers, modal drivers, fixtures, scenarios |
| [testing/ui_smoke.md](testing/ui_smoke.md) | UI smoke blocks A-E over UI Automation, the UI-test build, the AEAT pre-production run |
| [testing/smoke_test.md](testing/smoke_test.md) | Manual checklist: part 1 before a release, part 2 the post-update self-check |
| [modules/mainwindow.md](modules/mainwindow.md) | Main window, menus, save flow, the `src/app/` dialogs |
| [modules/sql_lite.md](modules/sql_lite.md) | Database functions, accounting lock |
| [modules/listado.md](modules/listado.md) | List viewer, InsertNewItem, GenListado |
| [modules/recog_prendas.md](modules/recog_prendas.md) | Pickup operations, search, PayDialog, void dialog |
| [modules/add_garment.md](modules/add_garment.md) | Add-garment workflow and prices |
| [modules/facturas.md](modules/facturas.md) | Supplier-invoice form |
| [modules/contabilidad.md](modules/contabilidad.md) | Report modes, quarter close and revert |
| [modules/imprimir.md](modules/imprimir.md) | Print orchestration, layouts, QR |
| [modules/printing.md](modules/printing.md) | ESC/POS core |
| [modules/printer/README.md](modules/printer/README.md) | Printer research dossier (model, control methods, command subset, plan) |
| [modules/backup.md](modules/backup.md) | Backups: API, retention, settings, wiring |
| [modules/uikit.md](modules/uikit.md) | Shared dialog style |
| [modules/verifactu/README.md](modules/verifactu/README.md) | Verifactu reference: architecture, API, configuration, schema, integration points, environments, errors |
| [modules/verifactu/verifactu-requirements.md](modules/verifactu/verifactu-requirements.md) | RD 1007/2023 and Orden HAC/1177/2024 requirements against the code |
| [modules/verifactu/rest_api.md](modules/verifactu/rest_api.md) | Gateway REST API as researched (code is authoritative) |
| [modules/verifactu/aeat-direct-investigation.md](modules/verifactu/aeat-direct-investigation.md) | Research: direct AEAT client versus the gateway |

## Skills and subagents

Skills (`.claude/skills/<name>/SKILL.md`; read the file when the skill applies):

| Skill | Purpose |
|-------|---------|
| `/tackle-issue` | Resolve a tracker item end to end |
| `/debugging` | Root-cause workflow for a reported bug, guarded by a regression test |
| `/update-docs` | Checklist to keep the docs current; size limits |
| `/update-skills` | Create or change skills and subagents |
| `/coding-guidelines` | Language, naming, Qt, DB and safety rules; `audit.md` is the audit checklist |
| `/release` | Manual only: release X.Y end to end |
| `/code-review`, `/security-review`, `/simplify`, `/init` | Built in |

Subagents (`.claude/agents/<name>.md`, run with `Agent` and `subagent_type`): `dead-code-finder`, `test-engineer`, `verifactu-compliance-auditor`, `guidelines-auditor`. Roles and tiers: [ai_agent_instructions.md](ai_agent_instructions.md#skills-and-subagents).

## Source files

| Module | Files | Role |
|--------|-------|------|
| `src/app/` | `main.cpp`, `commandline.h` | Entry point, `--settings`, `--self-check`, `--fake-aeat` (UI-test build only) |
| | `mainwindow.*` | Main window, built in code; ticket entry and save |
| | `verifactucoordinator.*` | The window's Verifactu side without widgets: the integration, pending submits, the bounded wait, the connection tests |
| | `cancelinvoicedialog.*`, `rectifyinvoicedialog.*` | Anular / Rectificar factura (R1-R5) |
| | `pendingsubmitsdialog.*`, `refusedinvoicesdialog.*` | Start-up recovery of PENDIENTE rows; the list behind the red status-bar count |
| | `aeatexport.*`, `aeatexportdialog.*` | Exportar registros AEAT (XML) |
| | `selfcheck.*`, `selfcheckdialog.*` | Comprobar tras actualizar |
| | `instancelock.*` | One copy per database (`QLockFile`) |
| `src/appsettings/` | `appsettings.*`, `applanguage.*` | Settings, UI language |
| `src/settingsdialog/` | `settingsdialog.*` | Configuración dialog |
| `src/logging/` | `applogger.*` | Debug log |
| `src/textformat/` | `textformat.*` | Money, decimal and stored-date text (`displayMoney`, `moneyParse`, `kDbDateFormat`, ...) |
| `src/sql_lite/` | `sql_lite.*`, `ingresos_schema.h` | Database functions, migrations, shared helpers, `ingresos` column indices |
| `src/listado/` | `listado.*`, `insertnewitem.*`, `genlistado.*` | List viewer, insert dialog, list PDF |
| `src/recog_prendas/` | `recog_prendas.*`, `pay_dialog.*`, `voidgarmentsdialog.*`, `aeatreconcilesummary.*`, `invoicingpolicy.cpp` | Pickup panel, partial payment, void garments, text of the "Consultar en AEAT" dialog |
| `src/add_garment/` | `add_garment.*` | Add garments to a ticket |
| `src/facturas/` | `facturas.*` | Supplier-invoice form |
| `src/contabilidad/` | `contabilidad.*` | Accounting reports and quarter close |
| `src/imprimir/` | `imprimir.*` | Print orchestration |
| `src/printing/` | `escposbuilder.*`, `ticketrenderer.*` | Pure: ESC/POS bytes, layout |
| | `thermalprinter.*`, `printerqueue.*` | RAW spooler transport, default printer name |
| | `printerstatus.*`, `statusapiprinter.*` | Epson ASB decoder, optional Status API transport |
| `src/verifactu/` | `verifactuintegration.*`, `verifactubackend.h`, `verifactutypes.h` | Facade, backend interface, estado and result types |
| | `verifactumanager.*`, `verifacturesponse.*`, `verifactuconfig.*`, `verifactuinvoice.*`, `verifactutaxitem.*` | Gateway client and its models |
| | `aeatdirectbackend.*` | Direct client flow |
| | `aeatrecord.*`, `aeathash.*`, `aeatqr.*`, `aeatresponse.*` | Records and envelopes, huella and QR URL, local QR, replies |
| | `aeatstore.*`, `aeatchaincheck.*` | `aeat_*` tables, stored-chain verifier |
| | `aeatchainsync.*` | The chain rules of a sync (`AeatChainSync::plan`): which record is the tip, restart, installation number; plain data, no store |
| | `aeatcertificate.*`, `aeattransport.*` | Certificate (store or .pfx), WinHTTP SOAP |
| | `aeatselftest.*`, `aeatconnectionprobe.*` | Test-environment rehearsal; query-only connection test |
| | `producerdeclaration.*`, `testhostguard.h` | Declaración responsable (art. 15); under `LAIDEAL_TEST=1` the transports refuse any host but this machine |
| `src/backup/` | `backup_manager.*` | Backups |
| `src/updater/` | `updater.*`, `updaterdialog.*` | GitHub-releases updater |
| `src/reporthtml/` | `reporthtml.*` | PDF scaffolding and `writePdf` |
| `src/uikit/` | `uikit.*` | Dialog style: `setUpDialog`, `heading`, `introPanel`, `ResultPanel`, buttons, `dateEdit`, HTML helpers |
| `src/tableview/` | `tableview.*`, `mysortfilterproxymodel.*`, `filterwidget.*`, `numberformatdelegate.*`, `textcolordelegate.*`, `linkdelegate.*`, `ingresoscolumns.*` | Table utilities (one library) |
| `src/third_party/qrcodegen/` | `qrcodegen.*` | Vendored QR encoder |

## Build, CI and tests

| File | Purpose |
|------|---------|
| `CMakeLists.txt` | Root project (version, subdirectories, `LAIDEAL_UI_TEST`) |
| `src/<module>/CMakeLists.txt` | One static library per module; `src/app/` also builds `laideal` |
| `make.ps1`, `make.cmd` | Task runner: build, test, UI smoke, AEAT runs ([README](../README.md#build)) |
| `.github/workflows/ci.yml` | `build`, `ui-smoke`, `release` jobs ([CONTRIBUTING](../CONTRIBUTING.md#what-ci-runs)) |
| `.github/workflows/docs.yml` | Builds the MkDocs site and publishes it to GitHub Pages |
| `.github/actions/setup-toolchain/` | Qt, MinGW, Ninja, Inno Setup and ccache for every job |
| `.github/scripts/Render-TestSummary.ps1` | Per-suite test report for the CI step summary |
| `mkdocs.yml`, `pyproject.toml` | Documentation site configuration and its tooling |
| `releases/` | `package.ps1` (packaging recipe), `release.ps1` / `release.bat` (local fallback), `laideal.iss` (installer) |
| `resources/` | `laideal.qrc` (icon, release notes, Qt translation), `laideal.rc.in`, `i18n/` |
| `tests/CMakeLists.txt` | 35 CTest tests (`grep -c "add_test(NAME" tests/CMakeLists.txt`): Qt Test suites plus the `aeat_xsd` schema check. `updater_dialog` is the exe of `test_versiondialog.cpp`, `version_compare` the exe of `test_updater.cpp`. Each suite: [testing/unit_suites.md](testing/unit_suites.md) |
| `tests/support/` | Fake Verifactu and AEAT servers, `modalautocloser.h`, `modaldriver.h`, `testschema.h`, `e2efixture.h`, `escpospreview.h` |
| `tests/tools/` | `fake_aeat_server`, `escpos_preview`, `aeat_query_dump` (saves AEAT's raw reply to a query; test environment, local use); built with the tests, not CTest suites |
| `tests/ui/` | Python UI smoke test (`smoke.py`, `block_a.py` to `block_e.py`, `actions.py`, `laideal_ui.py`, `seed.py`, `printer.py`, `aeat_preproduction.py`) |
| `tests/fixtures/`, `tests/aeat/` | AEAT replies, XSDs, test certificates; XSD validator |

## Glossary

| Term | Meaning |
|------|---------|
| Recibo / ticket | One client visit: N rows in `ingresos` sharing one `n_recibo` |
| Factura | The simplified sales invoice for a paid ticket (built from `ingresos`, sent to AEAT); or the supplier invoice of the Facturas form (stored in `gastos`) |
| Prenda | A garment item (clothing, rug, curtain) |
| Limp. / Plan. | Cleaning / ironing service |
| Trimestre | Quarter: the unit of the accounting lock and reports |
| `accounting_closures` | The quarter-close history; a row is locked when its date is in a closed quarter |
| `edit_lock` | Legacy lock column, replaced by `accounting_closures` |
| `hash` | 16-character id of an `ingresos` row within its ticket |
| CSV (Verifactu) | The security code AEAT returns per invoice |
| Huella | The chained SHA-256 of each record (`verifactu_hash`) |
| ServiceKey | The gateway's API key; external, stored encrypted (DPAPI) in the settings file |
| TESTING / PRODUCTION | Gateway environments; the direct client uses AEAT pre-production / production |
| `VerifactuEstado` | Enum of `verifactu_estado` ([architecture.md](architecture.md#database-schema)) |

## Topics

| Topic | Where |
|-------|-------|
| Ticket save flow | `src/app/mainwindow.cpp` (`saveTicket`, `submitAndPrint`); [architecture.md](architecture.md#ticket-save), [modules/mainwindow.md](modules/mainwindow.md) |
| Configuration keys | [../README.md](../README.md#configuration); `src/appsettings/appsettings.cpp` |
| Report folders | `AppSettings::reportsRoot`, `contabilidadPath`, `listadosPrendasPath`, `listadosGastosPath` |
| Printer queue and paper width | `AppSettings::printerName`, `paperWidthMm`; Configuración -> General |
| Database path | `AppSettings::dbPath`, set in `main.cpp` |
| Verifactu submit during save | `VerifactuCoordinator::submitInvoice` (`src/app/verifactucoordinator.*`); [verifactu/README.md](modules/verifactu/README.md) (Integration points) |
| QR on the printed ticket | `src/imprimir/imprimir.cpp` (`resolveQrCode`, `buildTicket`); `EscPosBuilder::rasterImage`; the legal leyenda prints below it when `verifactu_estado` is `ENVIADA` |
| Quarter lock | `quarterIsClosed`, `closeQuarter`, `reopenQuarter`, `quarterClosures` in `sql_lite.h` |
| Accounting with cancelled invoices | `kIngresosIncomeWhere`, `incomeTicketsBetweenDates` in `sql_lite.cpp`; [modules/contabilidad.md](modules/contabilidad.md) |
| Print flow and ESC/POS | [modules/imprimir.md](modules/imprimir.md), [modules/printing.md](modules/printing.md); command reference `modules/printer/03_escpos_command_reference.md`; code page `toCp858` in `escposbuilder.cpp` |
| Ticket conditions (RGPD, RD 1453/1987) | `ticketrenderer.cpp` (`copyForClient`); [modules/imprimir.md](modules/imprimir.md) |
| Search ignoring accents | `MySortFilterProxyModel::setSearchText` (`removeDiacritics`, `searchKey`) |
| Debug log | `~/.laideal.log`; `AppLogger::logFilePath()`; Ayuda -> Log de depuración shows the path |
| `verifactu_estado` values | `src/verifactu/verifactutypes.h` |
| `verifactu_*` columns, schema | [architecture.md](architecture.md#database-schema), [verifactu/README.md](modules/verifactu/README.md) |
| AEAT XML export (Art. 14.1) | `AeatExportDialog`, `aeatexport.cpp`, `aeatExportRecords` in `sql_lite.cpp`; envelope in [verifactu/README.md](modules/verifactu/README.md) |
| Huella (Art. 12) | `verifactu_hash`; computed in `aeathash.cpp`; the gateway's is parsed in `verifacturesponse.cpp` |
| Gateway REST fields | [modules/verifactu/rest_api.md](modules/verifactu/rest_api.md) |
| Updater | `src/updater/`; setting `updater.check_on_startup`; installer `releases/laideal.iss` |
| Backups | `src/backup/backup_manager.*`, `docs/modules/backup.md`; Archivo -> Hacer copia de seguridad ahora |
| Release notes in the app | `MainWindow::on_actionNotas_de_la_version_triggered`; bundled by `resources/laideal.qrc`; the same file is shown by the installer |
| Add a module | `src/<name>/CMakeLists.txt` and `add_subdirectory` in the root `CMakeLists.txt` |
| Workflow, release | [../CONTRIBUTING.md](../CONTRIBUTING.md) |
| Open issues | [progress_tracker.md](progress_tracker.md) |
| Version history | `releases_notes.txt`, `releases_notes_es.txt` |
