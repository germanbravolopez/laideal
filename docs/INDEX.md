# Index — La Ideal

**Use Ctrl+F to find any topic, file, concept, or function.** This is the project-wide quick-find reference.

---

## Documentation Files

| File | What's in it |
|------|-------------|
| `docs/ai_agent_instructions.md` | **Start here** — project briefing, critical issues, file map, agent rules |
| `docs/architecture.md` | Module details, DB schema, data flow, known issues, dependencies |
| `docs/progress_tracker.md` | Current status, blocking / open issues, backlog |
| `docs/completed_milestones.md` | Completed milestones since 11.0 (newest first) |
| `docs/completed_milestones_archive.md` | Milestones of 10.13 and before |
| `docs/reviews/2026-10-19-pre-12.0/` | Pre-12.0 deep review: one report per `src/` module plus the integration report with the prioritised backlog (P0 / P1 filed in the tracker) |
| `CONTRIBUTING.md` (root) | **How to contribute**: rules for every change (branch, test, compliance review, docs, commit style), the development workflow (branches `develop` / `master`, version bump, release PR, hotfix) and the release procedure (CI release job, local `release.ps1` fallback) |
| `docs/testing/README.md` | **Testing overview** — the five layers (unit suites, end-to-end bench, UI smoke test, manual smoke test, post-update self-check), how to run them locally and in CI, conventions, patterns, adding a suite, CI toolchain |
| `docs/testing/unit_suites.md` | What every `tests/test_*.cpp` suite covers, grouped by module |
| `docs/testing/e2e.md` | End-to-end test bench: safety rules, `FakeVerifactuServer`, `ModalAutoCloser`, `ModalDriver`, `testschema.h`, `e2efixture.h`, the `test_e2e_verifactu` and `test_e2e_app` scenarios |
| `docs/testing/ui_smoke.md` | **Automated UI smoke test**: smoke blocks A-E on the installed UI-test build (CMake `LAIDEAL_UI_TEST`, `--fake-aeat`) driven over UI Automation (pywinauto) on a `windows-latest` runner, against the fake AEAT on a synthetic DB; captured printer queue; the local AEAT pre-production script; how Qt widgets show in UIA (nested dialogs, cell widgets, checkable cells, dates); `tests/ui/`, `tests/tools/`, the `ui-smoke` job of `.github/workflows/ci.yml` |
| `docs/testing/smoke_test.md` | **Smoke test** — part 1 before the release (look of screens / PDFs, AEAT pre-production), part 2 the shop user's *Comprobar tras actualizar* (`laideal.exe --self-check`) after installing |
| `src/app/instancelock.h/.cpp` | One copy of the app per database (`QLockFile` keyed on the database path) |
| `src/app/refusedinvoicesdialog.h/.cpp` | Facturas rechazadas: the list behind the red status-bar count; opens Recogida on the selected ticket |
| `src/app/selfcheck.h/.cpp`, `src/app/selfcheckdialog.h/.cpp` | Post-update self-check: copies the newest backup and the settings, runs M1/M2, asks M2b/M3/M5, writes the report, deletes the copies (`test_selfcheck`) |
| `docs/INDEX.md` | This file |
| `docs/README.md` | Docs folder navigation table |
| `docs/modules/mainwindow.md` | MainWindow methods, save flow, table column constants |
| `docs/modules/sql_lite.md` | DB free-function reference, usage patterns, accounting lock |
| `docs/modules/listado.md` | Generic list viewer, InsertNewItem dialog, GenListado PDF |
| `docs/modules/recog_prendas.md` | Garment pickup operations, UpdateDBop enum, search, partial-payment PayDialog (8.5+) |
| `docs/modules/facturas.md` | Supplier invoice form, validation, auto-calculation |
| `docs/modules/contabilidad.md` | Accounting report modes, period locking, revertirOn |
| `docs/modules/imprimir.md` | Receipt/invoice print orchestration (ESC/POS), layout modes, QR |
| `docs/modules/backup.md` | Automatic and manual SQLite backups: API, retention, settings, wiring (Verifactu Req. 4) |
| `docs/modules/uikit.md` | Shared dialog style (`UiKit`): window setup, explanation panel, result panel, buttons, HTML helpers |
| `docs/modules/printing.md` | ESC/POS printing core: EscPosBuilder, TicketRenderer, ThermalPrinter (RAW spooler) |
| `docs/modules/add_garment.md` | Add-garment workflow, price calculation, validation |
| `docs/modules/verifactu/README.md` | Verifactu module reference: architecture, public API, configuration, DB schema, integration points, environments, errors |
| `docs/modules/verifactu/rest_api.md` | The gateway's REST API as researched (may be outdated; the code is authoritative) |
| `docs/modules/verifactu/aeat-direct-investigation.md` | Research (Oct 2026): submitting to AEAT directly instead of via IreneSolutions - services, mutual TLS, huella and chain, what to build, legal duties, timing, recommendation |
| `docs/modules/verifactu/verifactu-requirements.md` | Legal-compliance audit: each RD 1007/2023 / Orden HAC/1177/2024 requirement mapped to La Ideal coverage status |
| `docs/modules/printer/README.md` | **Printer research dossier** (background for the shipped ESC/POS code): Epson TM-T20III model/specs, control methods, ESC/POS command subset, current-flow analysis, and the implementation plan (5 files + index). Runtime reference: `docs/modules/printing.md` |

## Skills (`.claude/skills/<name>/SKILL.md`)

Read the full skill file when the skill is relevant to your task.

| Skill | File | Purpose |
|-------|------|---------|
| `/tackle-issue` | `.claude/skills/tackle-issue/SKILL.md` | End-to-end workflow to resolve an open issue from `docs/progress_tracker.md` (read → plan → implement → update docs → commit) |
| `/update-docs` | `.claude/skills/update-docs/SKILL.md` | Update docs after any change |
| `/update-skills` | `.claude/skills/update-skills/SKILL.md` | Create or update skills (`.claude/skills/`) and subagents (`.claude/agents/`) |
| `/debugging` | `.claude/skills/debugging/SKILL.md` | Root-cause workflow for a reported bug: reproduce from report/log/DB copy/AEAT response, localize, reduce, fix the cause, guard with a regression test proven non-vacuous |
| `/coding-guidelines` | `.claude/skills/coding-guidelines/SKILL.md` | Language, naming, Qt, DB, and safety rules for all new code |
| `/release` | `.claude/skills/release/SKILL.md` | Manual-only (`disable-model-invocation`). Ship a release X.Y end-to-end: pre-flight, version bump commit, PR `develop` -> `master` (CI + review in parallel), auto-merge (the push to `master` runs `ci.yml`'s `build` then `release` job, which publishes the GitHub Release), back-merge into `develop`; hotfix variant; `release.ps1` is the local fallback |
| `/code-review` | Built-in | Review the current diff or a pull request |
| `/security-review` | Built-in | Security review of pending branch changes |
| `/simplify` | Built-in | Review changed code for quality and simplification |
| `/init` | Built-in | Initialise a CLAUDE.md file |

## Subagents

Project-specific agents callable via the `Agent` tool with `subagent_type: "<name>"`.

| Agent | File | Purpose |
|-------|------|---------|
| `dead-code-finder` | `.claude/agents/dead-code-finder.md` | Scan `src/` for methods declared in headers but never called. Knows about Qt auto-connect / virtual overrides / signals and the deliberate test hooks so it doesn't false-flag them. Reports to the main agent, which removes the dead code; no report file is kept. |
| `test-engineer` | `.claude/agents/test-engineer.md` | Qt Test + CTest specialist: finds the coverage gap for a change/module and writes tests using the project seam patterns (pure helper, `QTemporaryDir` SQLite, AEAT response fixture, offscreen QPA), proving each fails without the fix. Preloads `/coding-guidelines`. |
| `verifactu-compliance-auditor` | `.claude/agents/verifactu-compliance-auditor.md` | Read-only review of a diff against the ten requirements in `docs/modules/verifactu/verifactu-requirements.md` (inalterability, numbering, hash chain, retention, estado, QR/text). Delegated proactively for changes touching invoices / `verifactu_*` / numbering / printed QR. |
| `guidelines-auditor` | `.claude/agents/guidelines-auditor.md` | Runs the coding-guidelines audit (`.claude/skills/coding-guidelines/audit.md`: grep checklist, severity tiers, accepted exceptions) over `src/` and returns tiered findings |

## Source Files

| Module | Header | Implementation |
|--------|--------|---------------|
| Entry point | `src/app/main.cpp` | — |
| Debug logger | `src/logging/applogger.h` | `src/logging/applogger.cpp` |
| App settings singleton | `src/appsettings/appsettings.h` | `src/appsettings/appsettings.cpp` |
| Settings dialog | `src/appsettings/settingsdialog.h` | `src/appsettings/settingsdialog.cpp` |
| UI language (Qt dialogs, release notes, installer choice) | `src/appsettings/applanguage.h` | `src/appsettings/applanguage.cpp` |
| Main window | `src/app/mainwindow.h` | `src/app/mainwindow.cpp` |
| Invoice cancellation dialog (paid/AEAT) | `src/app/cancelinvoicedialog.h` | `.cpp` |
| Void unpaid garments dialog (local, issue #40; opened from Recogida → Anular prendas…) | `src/recog_prendas/voidgarmentsdialog.h` | `.cpp` |
| Invoice rectification dialog (R1-R5) | `src/app/rectifyinvoicedialog.h` | `.cpp` |
| Pending Verifactu submits recovery dialog (startup) | `src/app/pendingsubmitsdialog.h` | `.cpp` |
| AEAT records export (XML envelope writer) | `src/app/aeatexport.h` | `.cpp` |
| AEAT records export dialog | `src/app/aeatexportdialog.h` | `.cpp` |
| Refused invoices dialog (list behind the red status-bar count) | `src/app/refusedinvoicesdialog.h` | `.cpp` |
| Post-update self-check (`--self-check`): checks and window | `src/app/selfcheck.h`, `src/app/selfcheckdialog.h` | `.cpp` |
| One copy per database | `src/app/instancelock.h` | `.cpp` |
| Command-line switch values (`--settings`) | `src/app/commandline.h` | - |
| Database API | `src/sql_lite/sql_lite.h` | `src/sql_lite/sql_lite.cpp` |
| Generic list viewer | `src/listado/listado.h` | `src/listado/listado.cpp` |
| List row insert dialog | `src/listado/insertnewitem.h` | `.cpp` |
| List data generator | `src/listado/genlistado.h` | `.cpp` |
| Garment pickup panel | `src/recog_prendas/recog_prendas.h` | `.cpp` |
| Partial-payment dialog | `src/recog_prendas/pay_dialog.h` | `.cpp` |
| Formal invoice form | `src/facturas/facturas.h` | `.cpp` |
| Accounting reports | `src/contabilidad/contabilidad.h` | `.cpp` |
| Receipt/invoice print orchestration | `src/imprimir/imprimir.h` | `.cpp` |
| ESC/POS byte builder (pure) | `src/printing/escposbuilder.h` | `.cpp` |
| Ticket layout renderer (pure) | `src/printing/ticketrenderer.h` | `.cpp` |
| Thermal printer RAW transport | `src/printing/thermalprinter.h` | `.cpp` |
| Printer ASB status decoder (pure) | `src/printing/printerstatus.h` | `.cpp` |
| Epson Status API transport (optional) | `src/printing/statusapiprinter.h` | `.cpp` |
| Add garments to ticket | `src/add_garment/add_garment.h` | `.cpp` |
| Verifactu facade | `src/verifactu/verifactuintegration.h` | `.cpp` |
| Verifactu gateway REST manager | `src/verifactu/verifactumanager.h` | `.cpp` |
| Verifactu estado enum, `VerifactuResult` | `src/verifactu/verifactutypes.h` | - |
| Gateway reply parser (pure) | `src/verifactu/verifacturesponse.h` | `.cpp` |
| Verifactu config | `src/verifactu/verifactuconfig.h` | `.cpp` |
| Verifactu backend interface (gateway / direct AEAT) | `src/verifactu/verifactubackend.h` | - |
| AEAT record hash (huella) + QR URL, direct client | `src/verifactu/aeathash.h` | `.cpp` |
| Stored AEAT chain verifier (art. 8.1 detection), direct client | `src/verifactu/aeatchaincheck.h` | `.cpp` |
| AEAT records + SOAP envelopes, direct client | `src/verifactu/aeatrecord.h` | `.cpp` |
| AEAT replies, record summary, chain tip | `src/verifactu/aeatresponse.h` | `.cpp` |
| AEAT chain head + record outbox (tables aeat_chain / aeat_records) | `src/verifactu/aeatstore.h` | `.cpp` |
| Verifactu QR drawn locally (vendored qrcodegen) | `src/verifactu/aeatqr.h` | `.cpp` |
| Owner's certificate (Windows store / .pfx) | `src/verifactu/aeatcertificate.h` | `.cpp` |
| WinHTTP SOAP transport with client certificate | `src/verifactu/aeattransport.h` | `.cpp` |
| Direct AEAT backend | `src/verifactu/aeatdirectbackend.h` | `.cpp` |
| Direct AEAT proof of concept (self-test) | `src/verifactu/aeatselftest.h` | `.cpp` |
| Declaración responsable (art. 15 Orden HAC/1177/2024) | `src/verifactu/producerdeclaration.h` | `.cpp` |
| Verifactu invoice model | `src/verifactu/verifactuinvoice.h` | `.cpp` |
| Verifactu tax-item model | `src/verifactu/verifactutaxitem.h` | `.cpp` |
| In-app updater (GitHub releases) | `src/updater/updater.h` | `.cpp` |
| Updater dialog | `src/updater/updaterdialog.h` | `.cpp` |
| Automated DB backup (Verifactu Req. 4) | `src/backup/backup_manager.h` | `.cpp` |
| Shared PDF report scaffolding (style + header + euro format) and the PDF writer `writePdf` (opens the file; `setOpenGeneratedReports(false)` in tests) | `src/reporthtml/reporthtml.h` | `.cpp` |
| Shared dialog style ([modules/uikit.md](modules/uikit.md)): `UiKit::setUpDialog`, `heading`, `introPanel`, `ResultPanel`, `primaryButton` / `secondaryButton`, `dateEdit`, `closeRow`, `okHtml` / `warnHtml` / `errorHtml`, `fileLinkHtml` | `src/uikit/uikit.h` | `.cpp` |
| Sort/filter proxy (+ diacritic search) | `src/tableview/mysortfilterproxymodel.h` | `.cpp` |
| Filter widget | `src/tableview/filterwidget.h` | `.cpp` |
| Custom table view | `src/tableview/tableview.h` | `.cpp` |
| Number format delegate | `src/tableview/numberformatdelegate.h` | `.cpp` |
| Text colour delegate | `src/tableview/textcolordelegate.h` | `.cpp` |
| Ingresos date-column placement | `src/tableview/ingresoscolumns.h` | `.cpp` |
| Link (URL) delegate | `src/tableview/linkdelegate.h` | `.cpp` |

## Build Files

| File | Purpose |
|------|---------|
| `CMakeLists.txt` | Root CMake — project 11.2, adds all subdirectories |
| `src/app/CMakeLists.txt` | Main executable target (`laideal`) |
| `src/tableview/CMakeLists.txt` | Single `tableview` static library (TableView, MySortFilterProxyModel, FilterWidget, NumberFormatDelegate, TextColorDelegate, LinkDelegate) |
| `src/listado/CMakeLists.txt` | `listado` static library; links `tableview` as PUBLIC |
| `src/<module>/CMakeLists.txt` | Per-module static library targets (incl. `src/printing` — ESC/POS, links `winspool` on Windows) |
| `tests/` (31 CTest tests: 31 Qt Test suites + the `aeat_xsd` schema check, incl. the `test_e2e_verifactu` and `test_e2e_app` end-to-end bench with `tests/support/` fake Verifactu server + pop-up closer + modal-dialog driver + shared fixture + `testschema.h`, the shop schema built through `migrateDatabase`) | `test_sql_lite` (+`garmentImporte`, DB write seams, `garmentIsLocallyVoidable`/`voidGarmentRow`), `test_mysortfilterproxymodel`, `test_textcolordelegate` (`classify` colour rule incl. Anulado→green), `test_verifactu_response`, `test_verifactu_models`, `test_aeat_hash` (official AEAT huella / QR vectors), `test_aeat_chaincheck` (stored-chain verifier), `test_aeat_record` + `aeat_xsd` (records against the official XSDs), `test_aeat_response`, `test_aeat_store`, `test_aeat_qr`, `test_aeat_direct_backend` + `test_e2e_aeat_direct` (against `support/fakeaeatserver`), `test_appsettings` (DPAPI + `loadFrom` getters), `test_settingsdialog` (service-key show/hide toggle, direct-AEAT group and production guards, offscreen), `test_facturas` (IVA split), `test_genlistado`, `test_backup_manager`, `test_contabilidad`, `test_escpos` (ESC/POS builder + renderer + `PrinterStatus` ASB decode), `test_ticket_preview` (renders sample recibo/factura to PNG + ASCII), `test_reporthtml`, `test_versioncompare` (the exe of `test_updater.cpp`; CTest name `version_compare`), `test_uikit` (the Enter rules), `test_instancelock`, `test_selfcheck`, `test_add_garment`, `test_applogger`. Run `ctest --test-dir build` |
| `tests/ui/` (Python + pywinauto) | The automated UI smoke test: `smoke.py` (runner), `block_a.py`…`block_e.py`, `actions.py` (operator flows), `laideal_ui.py` (UIA helpers), `seed.py`, `printer.py` (capturing printer queue), `aeat_preproduction.py` (local AEAT pre-production run + `--dry-run`) - see `docs/testing/ui_smoke.md` |
| `tests/tools/` | `fake_aeat_server` (the fake AEAT as a process) and `escpos_preview` (decodes captured ESC/POS jobs with `tests/support/escpospreview.h`, shared with `test_ticket_preview`); built with the tests, not CTest suites |
| `.github/scripts/Render-TestSummary.ps1` | Renders the foldable per-suite/per-method test report from `build/test-results-*.xml` into the GitHub step summary, and appends the `test_ticket_preview` ASCII receipt in a fenced block (GitHub strips inline images from summaries; the graphical PNG renders live in `docs/modules/printer/` + the `ticket-previews` artifact). Called by the `ci.yml` build job; also runnable locally. |
| `mkdocs.yml` | MkDocs Material config for the documentation site: theme (light/dark toggle, search), `docs/` tree as nav, and `validation:` settings that demote cross-repo links (to `src/` + root files) to build warnings so a non-strict `mkdocs build` stays green |
| `pyproject.toml` | Hosts the docs build tooling only (no Python package shipped): the `docs` optional-dependency group (`pip install ".[docs]"` → `mkdocs-material`) |
| `.github/workflows/docs.yml` | Builds the MkDocs site and publishes it to GitHub Pages via the official flow (`upload-pages-artifact` + `deploy-pages`). **site** job on push/PR to `develop` / `master` (PR run = pre-merge publication test); **deploy** job on master push, then verifies publication by polling the `page_url`. Needs repo Pages source = "GitHub Actions" |
| `.github/workflows/ci.yml` | CI + UI smoke + Release in one workflow; triggers: PR to `develop` / `master`, push to `develop` / `master`, `workflow_dispatch` (`gh workflow run ci.yml --ref <branch>`); one run per commit (`cancel-in-progress`; the `skip-duplicate` job of a release PR `develop` -> `master` cancels the `develop` push run of the same commit). **`build` job**: Qt 6.4.3 MinGW + CMake + Ninja, `ctest`, uploads `laideal.exe` + `ticket-previews` PNG renders. **`ui-smoke` job** (PRs and manual runs): UI-test build + `tests/ui/smoke.py` blocks A-E. **`release` job** (`needs: build`; PR to `master` = dry run keeping the `installer` artifact, push to `master` = publishes): reuses the build exe -> `releases/package.ps1` -> creates the GitHub Release `X.Y` (and its tag). Required checks on `master`: `build`, `ui-smoke`, `release`. |
| `.github/actions/setup-toolchain/action.yml` | Composite action used by every ci.yml job: installs Qt 6.4.3, MinGW 11.2, Ninja, Inno Setup and ccache, with Qt/MinGW/ccache caching |
| `releases/package.ps1` | The one packaging recipe: refuses a UI-test build, `windeployqt` + runtime-DLL check, zip, Inno Setup installer + `.sha256`; used by the `ci.yml` release and ui-smoke jobs and by `release.ps1` |
| `releases/release.ps1` | Local release pipeline (configure + build + `ctest` + `package.ps1`); offline fallback for the `ci.yml` release job |
| `releases/laideal.iss` | Inno Setup installer recipe (paths relative to `releases/`; `/DMyAppVersion=X.Y`) |

## Key Concepts / Glossary

| Term | Definition |
|------|-----------|
| Recibo / Ticket | Customer visit receipt. One ticket = N rows in `ingresos` sharing the same `n_recibo` |
| Factura | Two meanings. The simplified sales invoice the shop issues for a paid ticket: built from the `ingresos` rows and sent to AEAT. The supplier invoice entered in the Facturas form: stored in `gastos` |
| Prenda | Garment item (clothing, rug, curtain, etc.) |
| Limp. | "Limpieza" — cleaning service |
| Plan. | "Plancha" — ironing service |
| accounting_closures | Table of Contabilidad's quarterly closes (append-only close / reopen history); a row is locked when its date is in a closed quarter |
| edit_lock | Legacy lock column in `ingresos` / `gastos`, replaced by `accounting_closures` (only read by its one-time migration) |
| hash | 16-char alphanumeric identifier on each `ingresos` row for deduplication |
| CSV (Verifactu) | "Código de Seguridad de la Versión" — the unique code AEAT issues per invoice |
| ServiceKey | API key for the Verifactu REST service. External — not in source control. Stored encrypted at rest in `~/.laideal_settings.json` via Windows DPAPI (per-user, `dpapi:v1:` prefix); `AppSettings` getter/setter decrypt/encrypt transparently |
| Trimestre | Quarter (Q1–Q4) — the unit for accounting lock and report generation |
| QR (Verifactu) | QR image generated per invoice, shown to client for AEAT validation |
| TESTING / PRODUCTION | Verifactu environments. TESTING uses fictitious data. PRODUCTION submits to AEAT |
| VerifactuEstado | `enum class` in `verifactutypes.h` representing the DB-persisted `verifactu_estado` value: `Unpaid`↔`"SIN COBRAR"`, `NotSubmitted`↔`"PENDIENTE"`, `Enviada`↔`"ENVIADA"`, `Anulada`↔`"ANULADA"`, `Rectificada`↔`"RECTIFICADA"`, `Error`↔`"ERROR"`. Use `verifactuEstadoToString()` / `verifactuEstadoFromString()` to convert |

## Topics → File Map

| Topic | Where to look |
|-------|--------------|
| Ticket save flow (complete) | `src/app/mainwindow.cpp` (`saveTicket`) + `docs/modules/mainwindow.md` |
| All configurable settings (database and report paths, printing, business info, Verifactu connection and certificate, updater, backup; not the IVA rate, which is fixed) | `src/appsettings/appsettings.h` + `docs/architecture.md` (AppSettings section) |
| Reports root and hardcoded subdirs (Contabilidad / Listados/Prendas / Listados/Gastos) | `src/appsettings/appsettings.h` (`reportsRoot`, `contabilidadPath`, `listadosPrendasPath`, `listadosGastosPath`) |
| Printer queue + paper width settings | `src/appsettings/appsettings.h` (`printerName`, `paperWidthMm`) — edited in SettingsDialog → General |
| DB path configuration | `src/appsettings/appsettings.h` (`dbPath`), set in `main.cpp` |
| Verifactu call during save | `src/app/mainwindow.cpp` (`verifactuSubmitInvoice`) |
| Verifactu QR on printed ticket | `src/imprimir/imprimir.cpp` (`resolveQrCode`, `buildTicket`) — pixmap from the save flow, drawn locally by the direct client (`qrBeforeReply`), or the gateway's REST `/GetQrCode`, rastered into the ESC/POS stream (`EscPosBuilder::rasterImage`); the legal verification leyenda prints below the QR when `verifactu_estado == ENVIADA` |
| Accounting lock check | `src/sql_lite/sql_lite.h` (`quarterIsClosed`, `closeQuarter`, `reopenQuarter`, `quarterClosures`) |
| Hash generation | `src/sql_lite/sql_lite.h` (`genHash16`) |
| Print flow (ESC/POS) | `docs/modules/imprimir.md` (orchestration) + `docs/modules/printing.md` (ESC/POS core) + `src/imprimir/imprimir.h` |
| ESC/POS command subset / code page (PC858) | `docs/modules/printer/03_escpos_command_reference.md` + `src/printing/escposbuilder.cpp` (`toCp858`) |
| Ticket conditions (RGPD, RD 1453/1987 clauses) | `src/printing/ticketrenderer.cpp` (`copyForClient` guard) + `docs/modules/imprimir.md` (General conditions block table) |
| How to add a new module | `CMakeLists.txt` + create `src/<name>/CMakeLists.txt` |
| Debug log file location | `src/logging/applogger.h` (`logFilePath()`) — `~/.laideal.log` |
| How to read customer bug logs | `~/.laideal.log` on customer machine; Ayuda → Log de depuración shows the path and an "Abrir archivo" button that opens the log directly |
| Table view utilities (sort, filter, delegates) | `src/tableview/` — single `tableview` CMake library |
| Diacritic-insensitive search (tildes) | `src/tableview/mysortfilterproxymodel.h` — `removeDiacritics()`, `searchKey()` + `setSearchText()` (one mode: accents and case ignored, every word) |
| Development workflow, release procedure | Root `CONTRIBUTING.md` |
| Gateway REST API fields (as researched) | `docs/modules/verifactu/rest_api.md` |
| Verifactu DB schema (ingresos verifactu_* columns) | `docs/architecture.md` (ingresos schema) + `docs/modules/verifactu/README.md` (DB persistence section) |
| Verifactu XML export for Hacienda (Art. 14.1) | `src/app/aeatexportdialog.cpp` (`AeatExportDialog`, opened by `MainWindow::on_actionExportar_registros_aeat_triggered`), records from `sql_lite::aeatExportRecords` (one per payment event), XML written by `src/app/aeatexport.cpp`; column `verifactu_xml` in `ingresos`; envelope format documented in `docs/modules/verifactu/README.md` (Integration points) |
| Verifactu chained hash (AEAT "Huella", Art. 12) | column `verifactu_hash` in `ingresos`; extraction in `parseVerifactuResponse()` (`verifacturesponse.cpp`, regex over `Return.Xml`); the direct client computes it (`aeathash.cpp`) → `VerifactuResult::rawHash` |
| verifactu_estado string values / VerifactuEstado enum | `src/verifactu/verifactutypes.h` (`VerifactuEstado` enum + `verifactuEstadoToString/FromString`) |
| Accounting correctness with cancelled invoices (ANULADA) | `src/sql_lite/sql_lite.cpp` (`kIngresosIncomeWhere`, `incomeTicketsBetweenDates`) + `docs/modules/contabilidad.md` |
| Verifactu integration points (save, retry, cancel, print) | `docs/modules/verifactu/README.md` (Integration points section) |
| Open issues and blockers | `docs/progress_tracker.md` |
| Known technical debt | `docs/architecture.md` (Known Issues) |
| Version history | `releases_notes.txt` |
| In-app updater (Ayuda → Buscar actualizaciones / startup check) | `src/updater/updater.h` + `src/updater/updaterdialog.h`; setting `updater.check_on_startup` in `~/.laideal_settings.json`; in-place install via `releases/laideal.iss` (`CloseApplications=yes`, `RestartApplications=yes`) |
| Automated SQLite backup (Verifactu Req. 4) + manual trigger (Archivo → Hacer copia de seguridad ahora) | `src/backup/backup_manager.h/.cpp`; settings `backup.enabled` / `backup.last_time` / `backup.last_version` (directory: `backups/` next to the DB); auto path scheduled from `MainWindow` constructor via `QTimer::singleShot(3000)` when `needsBackup()`; see `docs/modules/backup.md` |
| In-app version history (Ayuda → Notas de la versión) | `src/app/mainwindow.cpp` (`on_actionNotas_de_la_version_triggered`) reads the bundled `:/docs/releases_notes.txt` resource (declared in `resources/laideal.qrc`); the file is the same one Inno Setup's `InfoBeforeFile` shows at install time |
