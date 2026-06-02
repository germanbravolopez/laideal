# Index — La Ideal

**Use Ctrl+F to find any topic, file, concept, or function.** This is the project-wide quick-find reference.

---

## Documentation Files

| File | What's in it |
|------|-------------|
| `docs/AI_agent_instructions.md` | **Start here** — project briefing, critical issues, file map, agent rules |
| `docs/architecture.md` | Module details, DB schema, data flow, known issues, dependencies |
| `docs/progress_tracker.md` | What's done, blocking issues, in progress, completed milestones |
| `docs/dead_code_report.md` | Snapshot of unused methods in `src/` (regenerate with the [`dead-code-finder`](.claude/agents/dead-code-finder.md) agent) |
| `docs/INDEX.md` | This file |
| `docs/README.md` | Docs folder navigation table |
| `docs/modules/mainwindow.md` | MainWindow methods, save flow, table column constants |
| `docs/modules/sql_lite.md` | DB free-function reference, usage patterns, accounting lock |
| `docs/modules/listado.md` | Generic list viewer, InsertNewItem dialog, GenListado PDF |
| `docs/modules/recog_prendas.md` | Garment pickup operations, UpdateDBop enum, search, partial-payment PayDialog (8.5+) |
| `docs/modules/facturas.md` | Supplier invoice form, validation, auto-calculation |
| `docs/modules/contabilidad.md` | Accounting report modes, period locking, revertirOn |
| `docs/modules/imprimir.md` | Excel/print flow, layout modes, QXlsx dependency |
| `docs/modules/add_garment.md` | Add-garment workflow, price calculation, validation |
| `docs/modules/verifactu/README.md` | Verifactu module reference: architecture, public API, configuration, DB schema, integration points, environments, errors |
| `docs/modules/verifactu/rest_api.md` | AEAT REST API complete field reference |
| `docs/modules/verifactu/verifactu-requirements.md` | Legal-compliance audit: each RD 1007/2023 / Orden HAC/1177/2024 requirement mapped to La Ideal coverage status |

## Skills (Custom Slash Commands)

Read the full skill file when the skill is relevant to your task.

| Skill | File | Purpose |
|-------|------|---------|
| `/tackle-issue` | `.claude/commands/tackle-issue.md` | End-to-end workflow to resolve an open issue from `docs/progress_tracker.md` (read → plan → implement → update docs → commit) |
| `/update-docs` | `.claude/commands/update-docs.md` | Update docs after any change |
| `/update-skills` | `.claude/commands/update-skills.md` | Create or update slash command skills |
| `/coding-guidelines` | `.claude/commands/coding-guidelines.md` | Language, naming, Qt, DB, and safety rules for all new code |
| `/release` | `.claude/commands/release.md` | Ship a release X.Y end-to-end: pre-flight, version bump commit, PR-style merge to master, tag (which triggers `release.yml` CI to build + publish the GitHub Release), watch CI; `release.ps1` is the local fallback |
| `/review` | Built-in | Review a pull request |
| `/security-review` | Built-in | Security review of pending branch changes |
| `/simplify` | Built-in | Review changed code for quality and simplification |
| `/init` | Built-in | Initialise a CLAUDE.md file |

## Subagents

Project-specific agents callable via the `Agent` tool with `subagent_type: "<name>"`.

| Agent | File | Purpose |
|-------|------|---------|
| `dead-code-finder` | `.claude/agents/dead-code-finder.md` | Scan `src/` for methods declared in headers but never called. Knows about Qt auto-connect / virtual overrides / signals so it doesn't false-flag them. Output goes into `docs/dead_code_report.md`. |

## Source Files

| Module | Header | Implementation |
|--------|--------|---------------|
| Entry point | `src/app/main.cpp` | — |
| Debug logger | `src/logging/applogger.h` | `src/logging/applogger.cpp` |
| App settings singleton | `src/appsettings/appsettings.h` | `src/appsettings/appsettings.cpp` |
| Settings dialog | `src/appsettings/settingsdialog.h` | `src/appsettings/settingsdialog.cpp` |
| Main window | `src/app/mainwindow.h` | `src/app/mainwindow.cpp` |
| Invoice cancellation dialog | `src/app/cancelinvoicedialog.h` | `.cpp` |
| Invoice rectification dialog (R1-R5) | `src/app/rectifyinvoicedialog.h` | `.cpp` |
| Pending Verifactu submits recovery dialog (startup) | `src/app/pendingsubmitsdialog.h` | `.cpp` |
| Database API | `src/sql_lite/sql_lite.h` | `src/sql_lite/sql_lite.cpp` |
| Generic list viewer | `src/listado/listado.h` | `src/listado/listado.cpp` |
| List row insert dialog | `src/listado/insertnewitem.h` | `.cpp` |
| List data generator | `src/listado/genlistado.h` | `.cpp` |
| Garment pickup panel | `src/recog_prendas/recog_prendas.h` | `.cpp` |
| Partial-payment dialog | `src/recog_prendas/pay_dialog.h` | `.cpp` |
| Formal invoice form | `src/facturas/facturas.h` | `.cpp` |
| Accounting reports | `src/contabilidad/contabilidad.h` | `.cpp` |
| Print + Excel | `src/imprimir/imprimir.h` | `.cpp` |
| Add garments to ticket | `src/add_garment/add_garment.h` | `.cpp` |
| Verifactu facade | `src/verifactu/verifactuintegration.h` | `.cpp` |
| Verifactu REST manager | `src/verifactu/verifactumanager.h` | `.cpp` |
| Verifactu config | `src/verifactu/verifactuconfig.h` | `.cpp` |
| Verifactu invoice model | `src/verifactu/verifactuinvoice.h` | `.cpp` |
| Verifactu tax-item model | `src/verifactu/verifactutaxitem.h` | `.cpp` |
| In-app updater (GitHub releases) | `src/updater/updater.h` | `.cpp` |
| Updater dialog | `src/updater/updaterdialog.h` | `.cpp` |
| Automated DB backup (Verifactu Req. 4) | `src/backup/backup_manager.h` | `.cpp` |
| Shared PDF report scaffolding (style + header + euro format) | `src/reporthtml/reporthtml.h` | `.cpp` |
| Sort/filter proxy (+ diacritic search) | `src/tableview/mysortfilterproxymodel.h` | `.cpp` |
| Filter widget | `src/tableview/filterwidget.h` | `.cpp` |
| Custom table view | `src/tableview/tableview.h` | `.cpp` |
| Number format delegate | `src/tableview/numberformatdelegate.h` | `.cpp` |
| Text colour delegate | `src/tableview/textcolordelegate.h` | `.cpp` |
| Link (URL) delegate | `src/tableview/linkdelegate.h` | `.cpp` |
| Excel library | `QXlsx/` (entire directory) | third-party (vendored) — only a tiny local patch added: `Worksheet::setPageMargins` + `Document::setPageMargins` |

## Build Files

| File | Purpose |
|------|---------|
| `CMakeLists.txt` | Root CMake — project 8.0, adds all subdirectories |
| `src/app/CMakeLists.txt` | Main executable target (`laideal`) |
| `src/tableview/CMakeLists.txt` | Single `tableview` static library (TableView, MySortFilterProxyModel, FilterWidget, NumberFormatDelegate, TextColorDelegate, LinkDelegate) |
| `src/listado/CMakeLists.txt` | `listado` static library; links `tableview` as PUBLIC |
| `src/<module>/CMakeLists.txt` | Per-module static library targets |
| `QXlsx/CMakeLists.txt` | QXlsx library build |
| `tests/` (`test_sql_lite`, `test_mysortfilterproxymodel`, `test_verifactu_response`, `test_backup_manager`, `test_contabilidad`, `test_reporthtml`, `test_versioncompare`) | Qt Test + CTest suites: sql_lite + `garmentImporte`, proxy diacritic filter, `parseVerifactuResponse`, `BackupManager::backupsToPrune`, `Contabilidad::periodRangeFor`, `ReportHtml::formatEuro`, `Updater::compareVersions`. Run `ctest --test-dir build` |
| `.github/scripts/Render-TestSummary.ps1` | Renders the foldable per-suite/per-method test report from `build/test-results-*.xml` into the GitHub step summary (called by both workflows; also runnable locally) |
| `.github/workflows/ci.yml` | CI — builds on every push/PR (Qt 6.4.3 MinGW + CMake + Ninja), runs `ctest`, uploads `laideal.exe` artifact |
| `.github/workflows/release.yml` | Release CI — on `X.Y` tag push, builds + `ctest` (hard gate) + `windeployqt` + zip + Inno Setup installer + publishes the GitHub Release (reproduces `releases\release.ps1`) |
| `releases/release.ps1` | Local release pipeline (build + `windeployqt` + zip + installer); offline fallback for `release.yml` |
| `releases/laideal.iss` | Inno Setup installer recipe (paths relative to `releases/`; `/DMyAppVersion=X.Y`) |

## Key Concepts / Glossary

| Term | Definition |
|------|-----------|
| Recibo / Ticket | Customer visit receipt. One ticket = N rows in `ingresos` sharing the same `n_recibo` |
| Factura | Formal invoice (different from receipt). Stored in `gastos` table via Facturas form |
| Prenda | Garment item (clothing, rug, curtain, etc.) |
| Limp. | "Limpieza" — cleaning service |
| Plan. | "Plancha" — ironing service |
| edit_lock | Column in `ingresos`: `0` = editable, `1` = locked by accounting close |
| hash | 16-char alphanumeric identifier on each `ingresos` row for deduplication |
| CSV (Verifactu) | "Código de Seguridad de la Versión" — the unique code AEAT issues per invoice |
| ServiceKey | API key for the Verifactu REST service. External — not in source control. Stored encrypted at rest in `~/.laideal_settings.json` via Windows DPAPI (per-user, `dpapi:v1:` prefix); `AppSettings` getter/setter decrypt/encrypt transparently |
| Trimestre | Quarter (Q1–Q4) — the unit for accounting lock and report generation |
| QR (Verifactu) | QR image generated per invoice, shown to client for AEAT validation |
| TESTING / PRODUCTION | Verifactu environments. TESTING uses fictitious data. PRODUCTION submits to AEAT |
| VerifactuEstado | `enum class` in `verifactumanager.h` representing the DB-persisted `verifactu_estado` value: `NotSubmitted`↔`"PENDIENTE"`, `Enviada`↔`"ENVIADA"`, `Anulada`↔`"ANULADA"`, `Error`↔`"ERROR"`. Use `verifactuEstadoToString()` / `verifactuEstadoFromString()` to convert |

## Topics → File Map

| Topic | Where to look |
|-------|--------------|
| Ticket save flow (complete) | `src/app/mainwindow.cpp` (`saveTicket`) + `docs/modules/mainwindow.md` |
| All configurable settings (paths, IVA, printing toggle, business info) | `src/appsettings/appsettings.h` + `docs/architecture.md` (AppSettings section) |
| Reports root and hardcoded subdirs (Contabilidad / Listados/Prendas / Listados/Gastos) | `src/appsettings/appsettings.h` (`reportsRoot`, `contabilidadPath`, `listadosPrendasPath`, `listadosGastosPath`) |
| Fixed ticket file paths (~/.laideal_ticket.xlsx, ~/.laideal_print.vbs) | `src/appsettings/appsettings.h` (`ticketExcelPath`, `ticketPrintScriptPath`) — regenerated each print |
| DB path configuration | `src/appsettings/appsettings.h` (`dbPath`), set in `main.cpp` |
| Verifactu call during save | `src/app/mainwindow.cpp` (`verifactuSubmitInvoice`) |
| Verifactu QR on printed ticket | `src/imprimir/imprimir.cpp` (`resolveQrCode`, `createTicketExcel`) — pixmap from save flow or REST `/GetQrCode`; the legal verification leyenda is written below the QR when `verifactu_estado == ENVIADA` |
| Accounting lock check | `src/sql_lite/sql_lite.h` (`readLockForMonthAndYear`) |
| Hash generation | `src/sql_lite/sql_lite.h` (`genHash16`) |
| Print / Excel flow | `docs/modules/imprimir.md` + `src/imprimir/imprimir.h` |
| Ticket conditions (RGPD, RD 1453/1987 clauses) | `src/imprimir/imprimir.cpp` (`createTicketExcel` — `copyForClient` guard) + `docs/modules/imprimir.md` (General conditions block table) |
| How to add a new module | `CMakeLists.txt` + create `src/<name>/CMakeLists.txt` |
| Debug log file location | `src/logging/applogger.h` (`logFilePath()`) — `~/.laideal.log` |
| How to read customer bug logs | `~/.laideal.log` on customer machine; Archivo → Log de depuración… shows the path and an "Abrir archivo" button that opens the log directly |
| Table view utilities (sort, filter, delegates) | `src/tableview/` — single `tableview` CMake library |
| Diacritic-insensitive search (tildes) | `src/tableview/mysortfilterproxymodel.h` — `removeDiacritics()` + `setNormalizedFilter()` |
| Release procedure | Root `README.md` |
| Verifactu REST API fields | `docs/modules/verifactu/rest_api.md` |
| Verifactu DB schema (ingresos verifactu_* columns) | `docs/architecture.md` (ingresos schema) + `docs/modules/verifactu/README.md` (DB persistence section) |
| Verifactu XML export for Hacienda (Art. 14.1) | `src/app/mainwindow.cpp` (`on_actionExportar_registros_aeat_triggered`); column `verifactu_xml` in `ingresos`; envelope format documented in `docs/modules/verifactu/README.md` (Integration points) |
| Verifactu chained hash (AEAT "Huella", Art. 12) | column `verifactu_hash` in `ingresos`; extraction in `VerifactuManager::processResponse()` (regex over `Return.Xml`) → `VerifactuResult::rawHash` |
| verifactu_estado string values / VerifactuEstado enum | `src/verifactu/verifactumanager.h` (`VerifactuEstado` enum + `verifactuEstadoToString/FromString`) |
| Accounting correctness with cancelled invoices (ANULADA) | `src/sql_lite/sql_lite.cpp` (`totalPriceBetweenDates`) + `docs/modules/contabilidad.md` |
| Verifactu integration points (save, retry, cancel, print) | `docs/modules/verifactu/README.md` (Integration points section) |
| Open issues and blockers | `docs/progress_tracker.md` |
| Known technical debt | `docs/architecture.md` (Known Issues) |
| Version history | `releases_notes.txt` |
| In-app updater (Ayuda → Buscar actualizaciones / startup check) | `src/updater/updater.h` + `src/updater/updaterdialog.h`; setting `updater.check_on_startup` in `~/.laideal_settings.json`; in-place install via `releases/laideal.iss` (`CloseApplications=yes`, `RestartApplications=yes`) |
| Automated SQLite backup (Verifactu Req. 4) + manual trigger (Herramientas → Hacer copia de seguridad ahora...) | `src/backup/backup_manager.h/.cpp`; settings `backup.directory` / `backup.enabled` / `backup.last_time`; auto path scheduled from `MainWindow` constructor via `QTimer::singleShot(3000)` when `needsBackup()`; see `docs/modules/backup.md` |
| In-app version history (Ayuda → Notas de la versión) | `src/app/mainwindow.cpp` (`on_actionNotas_de_la_version_triggered`) reads the bundled `:/docs/releases_notes.txt` resource (declared in `resources/laideal.qrc`); the file is the same one Inno Setup's `InfoBeforeFile` shows at install time |
