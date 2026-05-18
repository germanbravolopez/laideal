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
| Main window | `src/app/mainwindow.h` | `src/app/mainwindow.cpp` |
| Database API | `src/sql_lite/sql_lite.h` | `src/sql_lite/sql_lite.cpp` |
| Generic list viewer | `src/Listado/listado.h` | `src/Listado/listado.cpp` |
| List row insert dialog | `src/Listado/insertnewitem.h` | `.cpp` |
| List data generator | `src/Listado/genlistado.h` | `.cpp` |
| Garment pickup panel | `src/recog_prendas/recog_prendas.h` | `.cpp` |
| Formal invoice form | `src/facturas/facturas.h` | `.cpp` |
| Accounting reports | `src/contabilidad/contabilidad.h` | `.cpp` |
| Print + Excel | `src/imprimir/imprimir.h` | `.cpp` |
| Add garments to ticket | `src/add_garment/add_garment.h` | `.cpp` |
| Verifactu facade | `src/verifactu/verifactuintegration.h` | `.cpp` |
| Verifactu REST manager | `src/verifactu/verifactumanager.h` | `.cpp` |
| Verifactu config | `src/verifactu/verifactuconfig.h` | `.cpp` |
| Verifactu invoice model | `src/verifactu/verifactuinvoice.h` | `.cpp` |
| Sort/filter proxy | `src/MySortFilterProxyModel/mysortfilterproxymodel.h` | `.cpp` |
| Filter widget | `src/FilterWidget/filterwidget.h` | `.cpp` |
| Custom table view | `src/TableView/tableview.h` | `.cpp` |
| Number format delegate | `src/NumberFormatDelegate/numberformatdelegate.h` | `.cpp` |
| Text colour delegate | `src/TextColorDelegate/textcolordelegate.h` | `.cpp` |
| Excel library | `QXlsx/` (entire directory) | third-party — do not modify |

## Build Files

| File | Purpose |
|------|---------|
| `CMakeLists.txt` | Root CMake — project 8.0, adds all subdirectories |
| `src/app/CMakeLists.txt` | Main executable target |
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

## Topics → File Map

| Topic | Where to look |
|-------|--------------|
| Ticket save flow (complete) | `src/app/mainwindow.cpp` (`saveTicket`) + `docs/modules/mainwindow.md` |
| DB path (and hardcoding issue) | `src/sql_lite/sql_lite.h` (`DB_PATH`) |
| Verifactu call during save | `src/app/mainwindow.cpp` (`verifactuSubmitInvoice`) |
| QR dialog implementation | `src/app/mainwindow.cpp` (`showQrToClient`) |
| Accounting lock check | `src/sql_lite/sql_lite.h` (`readLockForMonthAndYear`) |
| Hash generation | `src/sql_lite/sql_lite.h` (`genHash16`) |
| Print / Excel flow | `docs/modules/imprimir.md` + `src/imprimir/imprimir.h` |
| How to add a new module | `CMakeLists.txt` + create `src/<name>/CMakeLists.txt` |
| Release procedure | Root `README.md` |
| Verifactu REST API fields | `docs/modules/verifactu/rest_api.md` |
| Verifactu DB schema changes (SQL) | `docs/modules/verifactu/implementation_summary.md` |
| Verifactu step-by-step implementation | `docs/modules/verifactu/step_by_step_guide.md` |
| Open issues and blockers | `docs/progress_tracker.md` |
| Known technical debt | `docs/architecture.md` (Known Issues) |
| Version history | `releases_notes.txt` |
