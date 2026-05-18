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
| `docs/modules/verifactu/README.md` | Verifactu overview and quick start |
| `docs/modules/verifactu/INDEX.md` | Verifactu docs navigation and reading paths |
| `docs/modules/verifactu/GUIA_PASO_A_PASO.md` | Step-by-step Verifactu implementation guide (45 min) |
| `docs/modules/verifactu/RESUMEN_IMPLEMENTACION.md` | Verifactu architecture, DB schema SQL, roadmap |
| `docs/modules/verifactu/VERIFACTU_REST_API.md` | Verifactu REST API complete field reference |
| `docs/development/planning_verifactu.md` | Historical planning notes (raw Copilot conversation) |

## Skills (Custom Slash Commands)

Read the full skill file when the skill is relevant to your task.

| Skill | File | Purpose |
|-------|------|---------|
| `/update-docs` | `.claude/commands/update-docs.md` | Update docs after any change |
| `/update-skills` | `.claude/commands/update-skills.md` | Create or update slash command skills |
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
| Code examples (Verifactu) | `src/verifactu/EJEMPLO_IMPLEMENTACION.cpp` | — |
| Sort/filter proxy | `src/MySortFilterProxyModel/mysortfilterproxymodel.h` | `.cpp` |
| Filter widget | `src/FilterWidget/filterwidget.h` | `.cpp` |
| Custom table view | `src/TableView/tableview.h` | `.cpp` |
| Number format delegate | `src/NumberFormatDelegate/numberformatdelegate.h` | `.cpp` |
| Text colour delegate | `src/TextColorDelegate/textcolordelegate.h` | `.cpp` |
| Excel library | `QXlsx/` (entire directory) | third-party — do not modify |

## Build Files

| File | Purpose |
|------|---------|
| `CMakeLists.txt` | Root CMake — project v8.0, adds all subdirectories |
| `src/app/CMakeLists.txt` | Main executable target |
| `src/<module>/CMakeLists.txt` | Per-module static library targets |
| `QXlsx/CMakeLists.txt` | QXlsx library build |

## Key Concepts / Glossary

| Term | Definition |
|------|-----------|
| Recibo / Ticket | Customer visit receipt. One ticket = N rows in `ingresos` sharing the same `n_recibo` |
| Factura | Formal invoice (different from receipt). Stored in `facturas` table |
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
| Ticket save flow (complete) | `src/app/mainwindow.cpp:372` + `docs/architecture.md` (Data Flow) |
| DB path (and hardcoding issue) | `src/sql_lite/sql_lite.h:9` |
| Verifactu call during save | `src/app/mainwindow.cpp:307` |
| QR dialog implementation | `src/app/mainwindow.cpp:334` |
| Accounting lock check | `src/sql_lite/sql_lite.h` (`read_lock_for_month_and_year`) |
| Hash generation | `src/sql_lite/sql_lite.h` (`gen_hash_16`) |
| Print / Excel flow | `src/imprimir/imprimir.h` + `.cpp` |
| How to add a new module | `CMakeLists.txt` + create `src/<name>/CMakeLists.txt` |
| Release procedure | Root `README.md` |
| Verifactu REST API fields | `docs/modules/verifactu/VERIFACTU_REST_API.md` |
| Verifactu DB schema changes (SQL) | `docs/modules/verifactu/RESUMEN_IMPLEMENTACION.md` |
| Verifactu step-by-step implementation | `docs/modules/verifactu/GUIA_PASO_A_PASO.md` |
| Open issues and blockers | `docs/progress_tracker.md` |
| Known technical debt | `docs/architecture.md` (Known Issues) |
| Version history | `releases_notes.txt` |
