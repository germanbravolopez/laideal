# Index — La Ideal

**Use Ctrl+F to find any topic, file, concept, or function.** This is the project-wide quick-find reference.

---

## Documentation Files

| File | What's in it |
|------|-------------|
| `docs/AI_agent_instructions.md` | **Start here** — project briefing, critical issues, file map, agent rules |
| `docs/architecture.md` | Module details, DB schema, data flow, known issues, dependencies |
| `docs/progress_tracker.md` | What's done, blocking issues, in progress, completed milestones |
| `docs/INDEX.md` | This file |
| `docs/README.md` | Docs folder navigation table |
| `docs/modules/mainwindow.md` | MainWindow methods, save flow, table column constants |
| `docs/modules/sql_lite.md` | DB free-function reference, usage patterns, accounting lock |
| `docs/modules/listado.md` | Generic list viewer, InsertNewItem dialog, GenListado PDF |
| `docs/modules/recog_prendas.md` | Garment pickup operations, UpdateDBop enum, search |
| `docs/modules/facturas.md` | Supplier invoice form, validation, auto-calculation |
| `docs/modules/contabilidad.md` | Accounting report modes, period locking, revertirOn |
| `docs/modules/imprimir.md` | Excel/print flow, layout modes, QXlsx dependency |
| `docs/modules/add_garment.md` | Add-garment workflow, price calculation, validation |
| `docs/modules/verifactu/README.md` | Verifactu overview, architecture, key interface, status |
| `docs/modules/verifactu/step_by_step_guide.md` | Step-by-step Verifactu implementation guide |
| `docs/modules/verifactu/implementation_summary.md` | Class reference, DB schema SQL, security, roadmap |
| `docs/modules/verifactu/rest_api.md` | Verifactu REST API complete field reference |

## Skills (Custom Slash Commands)

Read the full skill file when the skill is relevant to your task.

| Skill | File | Purpose |
|-------|------|---------|
| `/update-docs` | `.claude/commands/update-docs.md` | Update docs after any change |
| `/update-skills` | `.claude/commands/update-skills.md` | Create or update slash command skills |
| `/coding-guidelines` | `.claude/commands/coding-guidelines.md` | Language, naming, Qt, DB, and safety rules for all new code |
| `/review` | Built-in | Review a pull request |
| `/security-review` | Built-in | Security review of pending branch changes |
| `/simplify` | Built-in | Review changed code for quality and simplification |
| `/init` | Built-in | Initialise a CLAUDE.md file |

## Source Files

| Module | Header | Implementation |
|--------|--------|---------------|
| Entry point | `src/app/main.cpp` | — |
| Debug logger | `src/logging/applogger.h` | `src/logging/applogger.cpp` |
| App settings singleton | `src/appsettings/appsettings.h` | `src/appsettings/appsettings.cpp` |
| Settings dialog | `src/appsettings/settingsdialog.h` | `src/appsettings/settingsdialog.cpp` |
| Main window | `src/app/mainwindow.h` | `src/app/mainwindow.cpp` |
| Invoice cancellation dialog | `src/app/cancelinvoicedialog.h` | `.cpp` |
| Database API | `src/sql_lite/sql_lite.h` | `src/sql_lite/sql_lite.cpp` |
| Generic list viewer | `src/listado/listado.h` | `src/listado/listado.cpp` |
| List row insert dialog | `src/listado/insertnewitem.h` | `.cpp` |
| List data generator | `src/listado/genlistado.h` | `.cpp` |
| Garment pickup panel | `src/recog_prendas/recog_prendas.h` | `.cpp` |
| Formal invoice form | `src/facturas/facturas.h` | `.cpp` |
| Accounting reports | `src/contabilidad/contabilidad.h` | `.cpp` |
| Print + Excel | `src/imprimir/imprimir.h` | `.cpp` |
| Add garments to ticket | `src/add_garment/add_garment.h` | `.cpp` |
| Verifactu facade | `src/verifactu/verifactuintegration.h` | `.cpp` |
| Verifactu REST manager | `src/verifactu/verifactumanager.h` | `.cpp` |
| Verifactu config | `src/verifactu/verifactuconfig.h` | `.cpp` |
| Verifactu invoice model | `src/verifactu/verifactuinvoice.h` | `.cpp` |
| Sort/filter proxy (+ diacritic search) | `src/tableview/mysortfilterproxymodel.h` | `.cpp` |
| Filter widget | `src/tableview/filterwidget.h` | `.cpp` |
| Custom table view | `src/tableview/tableview.h` | `.cpp` |
| Number format delegate | `src/tableview/numberformatdelegate.h` | `.cpp` |
| Text colour delegate | `src/tableview/textcolordelegate.h` | `.cpp` |
| Link (URL) delegate | `src/tableview/linkdelegate.h` | `.cpp` |
| Excel library | `QXlsx/` (entire directory) | third-party — do not modify |

## Build Files

| File | Purpose |
|------|---------|
| `CMakeLists.txt` | Root CMake — project 8.0, adds all subdirectories |
| `src/app/CMakeLists.txt` | Main executable target (`laideal`) |
| `src/tableview/CMakeLists.txt` | Single `tableview` static library (TableView, MySortFilterProxyModel, FilterWidget, NumberFormatDelegate, TextColorDelegate, LinkDelegate) |
| `src/listado/CMakeLists.txt` | `listado` static library; links `tableview` as PUBLIC |
| `src/<module>/CMakeLists.txt` | Per-module static library targets |
| `QXlsx/CMakeLists.txt` | QXlsx library build |

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
| ServiceKey | API key for the Verifactu REST service. External — not in source control |
| Trimestre | Quarter (Q1–Q4) — the unit for accounting lock and report generation |
| QR (Verifactu) | QR image generated per invoice, shown to client for AEAT validation |
| TESTING / PRODUCTION | Verifactu environments. TESTING uses fictitious data. PRODUCTION submits to AEAT |
| VerifactuEstado | `enum class` in `verifactumanager.h` representing the DB-persisted `verifactu_estado` value: `NotSubmitted`, `Enviada`, `Anulada`, `Error`. Use `verifactuEstadoToString()` / `verifactuEstadoFromString()` to convert |

## Topics → File Map

| Topic | Where to look |
|-------|--------------|
| Ticket save flow (complete) | `src/app/mainwindow.cpp` (`saveTicket`) + `docs/modules/mainwindow.md` |
| All configurable settings (paths, IVA, printing toggle, business info) | `src/appsettings/appsettings.h` + `docs/architecture.md` (AppSettings section) |
| DB path configuration | `src/appsettings/appsettings.h` (`dbPath`), set in `main.cpp` |
| Verifactu call during save | `src/app/mainwindow.cpp` (`verifactuSubmitInvoice`) |
| Verifactu QR on printed ticket | `src/imprimir/imprimir.cpp` (`resolveQrCode`, `createTicketExcel`) — pixmap from save flow or REST `/GetQrCode` |
| Accounting lock check | `src/sql_lite/sql_lite.h` (`readLockForMonthAndYear`) |
| Hash generation | `src/sql_lite/sql_lite.h` (`genHash16`) |
| Print / Excel flow | `docs/modules/imprimir.md` + `src/imprimir/imprimir.h` |
| Ticket conditions (RGPD, RD 1453/1987 clauses) | `src/imprimir/imprimir.cpp` (`createTicketExcel` — `copyForClient` guard) + `docs/modules/imprimir.md` (General conditions block table) |
| How to add a new module | `CMakeLists.txt` + create `src/<name>/CMakeLists.txt` |
| Debug log file location | `src/logging/applogger.h` (`logFilePath()`) — `~/.laideal.log` |
| How to read customer bug logs | `~/.laideal.log` on customer machine; Herramientas → Log de depuración… opens the folder |
| Table view utilities (sort, filter, delegates) | `src/tableview/` — single `tableview` CMake library |
| Diacritic-insensitive search (tildes) | `src/tableview/mysortfilterproxymodel.h` — `removeDiacritics()` + `setNormalizedFilter()` |
| Release procedure | Root `README.md` |
| Verifactu REST API fields | `docs/modules/verifactu/rest_api.md` |
| Verifactu DB schema (ingresos verifactu_* columns) | `docs/architecture.md` (ingresos schema) + `docs/modules/verifactu/implementation_summary.md` |
| verifactu_estado string values / VerifactuEstado enum | `src/verifactu/verifactumanager.h` (`VerifactuEstado` enum + `verifactuEstadoToString/FromString`) |
| Accounting correctness with cancelled invoices (ANULADA) | `src/sql_lite/sql_lite.cpp` (`totalPriceBetweenDates`) + `docs/modules/contabilidad.md` |
| Verifactu step-by-step implementation | `docs/modules/verifactu/step_by_step_guide.md` |
| Open issues and blockers | `docs/progress_tracker.md` |
| Known technical debt | `docs/architecture.md` (Known Issues) |
| Version history | `releases_notes.txt` |
