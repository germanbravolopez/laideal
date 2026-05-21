# AI Agent Instructions — La Ideal

**Read this first. This file is your briefing.** Everything critical fits below. References lead you to detail when needed.

---

## Project in One Paragraph

La Ideal is a **Windows desktop laundry management app** — C++17 + Qt (5.15+ or 6.x), SQLite database, CMake build system. It manages garments, clients, receipts, accounting, invoices, and printing. From 8.0 it integrates **Verifactu** — mandatory AEAT digital invoicing for Spanish businesses. The DB lives at a hardcoded path on a specific machine.

## Critical Issues — Check Before Touching Anything

No blocking critical issues. See `docs/progress_tracker.md` for the full issue list.

## Build & Run

- **Requirements**: CMake 3.5+, Qt 5.15+/6.x, C++17
- **Qt modules needed**: `Widgets`, `Sql`, `PrintSupport`, `Network`
- Open `CMakeLists.txt` in Qt Creator, or: `cmake . && cmake --build .`
- Deploy: `windeployqt` + Inno Setup (see root `README.md`)

## File Map — Where to Find What

| What you need | Where |
|---------------|-------|
| Entry point | `src/app/main.cpp` |
| Debug log file | `src/logging/applogger.h` / `.cpp` |
| App settings (all configurable values) | `src/appsettings/appsettings.h` / `.cpp` |
| Settings dialog (UI to edit settings) | `src/appsettings/settingsdialog.h` / `.cpp` |
| Main window logic | `src/app/mainwindow.h` / `.cpp` |
| Invoice cancellation dialog | `src/app/cancelinvoicedialog.h` / `.cpp` |
| Ticket save flow | `src/app/mainwindow.cpp` (`saveTicket`) |
| All DB free functions | `src/sql_lite/sql_lite.h` / `.cpp` |
| Generic list viewer | `src/listado/listado.h` / `.cpp` |
| Garment pickup panel | `src/recog_prendas/recog_prendas.h` / `.cpp` |
| Table view utilities (TableView, FilterWidget, MySortFilterProxyModel, delegates) | `src/tableview/` — single `tableview` CMake library |
| Formal invoices form | `src/facturas/facturas.h` / `.cpp` |
| Accounting reports | `src/contabilidad/contabilidad.h` / `.cpp` |
| Print + Excel generation | `src/imprimir/imprimir.h` / `.cpp` |
| Add garments to ticket | `src/add_garment/add_garment.h` / `.cpp` |
| Verifactu facade | `src/verifactu/verifactuintegration.h` / `.cpp` |
| Verifactu REST manager | `src/verifactu/verifactumanager.h` / `.cpp` |
| Verifactu config | `src/verifactu/verifactuconfig.h` / `.cpp` |
| Verifactu invoice model | `src/verifactu/verifactuinvoice.h` / `.cpp` |
| Excel library (3rd-party) | `QXlsx/` — do not modify |
| Build config (root) | `CMakeLists.txt` |

## Documentation Map

| Document | Purpose |
|----------|---------|
| `README.md` | Repository root readme files with the project description and initial overview |
| `docs/INDEX.md` | **Quick-find**: every doc, source file, concept |
| `docs/architecture.md` | Module details, DB schema, data flow, dependencies |
| `docs/progress_tracker.md` | What's done, in progress, open issues |
| `docs/AI_agent_instructions.md` | This file |
| `docs/modules/verifactu/` | Full Verifactu implementation docs (4 files) |
| `docs/modules/*.md` | Per-module reference docs (MainWindow, sql_lite, listado, recog_prendas, etc.) |

## Skills — Read Fully When Relevant

| Skill | Invoke | When to use |
|-------|--------|-------------|
| Update documentation | `/update-docs` | After any feature, fix, or new insight |
| Create/update skills | `/update-skills` | When a new repeatable workflow is found |
| Coding guidelines | `/coding-guidelines` | Before writing any new code — language, naming, Qt, DB rules |
| Security review | `/security-review` | Before release or when touching auth/data |
| Code review | `/review` | Before merging significant changes |
| Simplify code | `/simplify` | After implementation, to improve quality |

**Rule**: When a skill is relevant to your current task, **read the full skill file before starting work**.

## Agent Obligations

1. **Run `/update-docs`** (or follow its steps manually) after every meaningful change.
2. **Update `docs/progress_tracker.md`** — add what you did at the top of the relevant section.
3. **No duplication** — if similar content exists in two docs, consolidate and link.
4. **Document size limits**: see `/update-docs` skill for thresholds and split rules.
5. **English only** in all documentation.
6. **Preserve history** — never delete progress tracker entries; move old ones to Archive.
7. **Avoid emojis** — do not use emojis in titles or other places

## Key Business Logic

- A **ticket (recibo)** = one client visit. N garment rows in `ingresos`, all sharing the same `n_recibo`.
- **Services**: "Limp." (limpieza/cleaning) and "Plan." (plancha/ironing) — each garment has a price per service.
- **Size**: `size` column multiplies unit price (e.g., rugs measured in m²).
- **Accounting lock**: once a quarter is locked, `ingresos` rows for that quarter cannot be added/modified.
- **Verifactu call** happens during ticket save in `on_bb_save_reset_clicked()` → `verifactuSubmitInvoice()`.
- **Hash**: each `ingresos` row has a 16-char hash (`genHash16()`) for deduplication.
- **Formal invoice (factura)**: separate from receipt. Entered via the Facturas form, stored in `facturas` table.
