# AI Agent Instructions - La Ideal

Read this first. It is the briefing; each row links to the detail.

## The project

A Windows desktop app for a dry-cleaning and laundry shop: C++17, Qt 6.4.3 (MinGW), SQLite, CMake. It manages tickets, garments, clients, accounting, invoices and printing, and sends every paid invoice to AEAT (Verifactu, Spanish digital invoicing). Settings live in `~/.laideal_settings.json` (`AppSettings`), including the database path.

Release-gating issues: none; the full list is in [progress_tracker.md](progress_tracker.md).

## Build, test, release

Commands: [README](../README.md#build) (`.\make build`, `.\make test`, `.\make regression`). Workflow, branches and release: [CONTRIBUTING.md](../CONTRIBUTING.md). Building needs the toolchain on `PATH` (PowerShell); `make.ps1` sets it.

## File map

| What | Where |
|------|-------|
| Entry point, command-line switches (`--settings`, `--self-check`) | `src/app/main.cpp`, `commandline.h` |
| Main window (built in code, no `.ui` file), ticket save flow | `src/app/mainwindow.*` (`on_pb_save_clicked`, `saveTicket`, `submitAndPrint`) |
| Facturas menu: cancel, rectify (R1-R5), export AEAT XML | `src/app/cancelinvoicedialog.*`, `rectifyinvoicedialog.*`, `aeatexport.*`, `aeatexportdialog.*` |
| Start-up recovery: PENDIENTE rows, refused invoices | `src/app/pendingsubmitsdialog.*`, `refusedinvoicesdialog.*` |
| Post-update check, one copy per database | `src/app/selfcheck*`, `src/app/instancelock.*` |
| Settings, settings dialog, UI language | `src/appsettings/` |
| Debug log (`~/.laideal.log`) | `src/logging/` |
| All database functions, schema, migrations, shared helpers | `src/sql_lite/sql_lite.*`, `ingresos_schema.h` |
| Generic list viewer, insert dialog, list PDF | `src/listado/` |
| Pickup panel, payment, void dialogs, AEAT query summary | `src/recog_prendas/` |
| Add garments to a ticket | `src/add_garment/` |
| Supplier-invoice form (stores in `gastos`) | `src/facturas/` |
| Accounting reports, quarter close | `src/contabilidad/` |
| Print orchestration; ESC/POS core (builder, renderer, RAW spooler, Status API) | `src/imprimir/`; `src/printing/` |
| Verifactu facade, backend interface, estado and result types | `src/verifactu/verifactuintegration.*`, `verifactubackend.h`, `verifactutypes.h` |
| Verifactu gateway (Irene Solutions REST) | `src/verifactu/verifactumanager.*`, `verifactuconfig.*`, `verifactuinvoice.*` |
| Verifactu direct AEAT client | `src/verifactu/aeat*`, `producerdeclaration.*` |
| Backups | `src/backup/` |
| In-app updater | `src/updater/` |
| Shared UI: dialog style, PDF scaffolding, table views | `src/uikit/`, `src/reporthtml/`, `src/tableview/` |
| Tests, fakes, UI scripts | `tests/` ([testing/README.md](testing/README.md)) |
| Build, CI, installer | `CMakeLists.txt`, `make.ps1`, `.github/workflows/`, `releases/` |

Every file, class and concept: [INDEX.md](INDEX.md).

## Documentation map

| Document | Owns |
|----------|------|
| [../README.md](../README.md) | What the project is, requirements, build / test commands, configuration keys, layout |
| [../CONTRIBUTING.md](../CONTRIBUTING.md) | Workflow, branching, commits, release procedure |
| [INDEX.md](INDEX.md) | Where to find any file, concept or topic |
| [architecture.md](architecture.md) | Modules and dependencies, data flows, database schema |
| [modules/](modules/) | One reference page per module; Verifactu in [modules/verifactu/](modules/verifactu/README.md) |
| [testing/](testing/README.md) | How to run and write tests, every suite, the e2e bench, UI smoke, manual checklist |
| [progress_tracker.md](progress_tracker.md) | Current status, open issues, backlog |
| [completed_milestones.md](completed_milestones.md) | Finished work, newest first (older in the archive) |
| [reviews/](reviews/) | Review reports kept for reference |

## Skills and subagents

Project skills are in `.claude/skills/<name>/SKILL.md`; read the whole file when one applies.

| Skill | Use |
|-------|-----|
| `/tackle-issue` | A tracker item, end to end |
| `/debugging` | Something is broken: reproduce, localize, fix the cause, guard with a test |
| `/update-docs` | After any change; also holds the size limits |
| `/update-skills` | Create or change a skill or subagent |
| `/coding-guidelines` | Before writing code |
| `/release X.Y` | Manual only: the release PR `develop` -> `master` |

Built in: `/code-review`, `/security-review`, `/simplify`.

Subagents (`.claude/agents/`, tier in the `model:` frontmatter; orchestration rules in `CLAUDE.md`):

| Agent | Model | Use |
|-------|-------|-----|
| `dead-code-finder` | haiku | Methods declared but never called; remove them or file them in the tracker |
| `test-engineer` | sonnet | Qt Test coverage for a change, each test proven to fail without the fix |
| `guidelines-auditor` | sonnet | The audit in `.claude/skills/coding-guidelines/audit.md` |
| `verifactu-compliance-auditor` | opus | Read-only legal review of changes touching invoices, `verifactu_*` columns, numbering or the QR |

## Obligations

1. After every meaningful change run `/update-docs`: tracker, then a milestone entry at the top of `completed_milestones.md`.
2. One owner per topic: link instead of repeating. Keep the size limits in `/update-docs`.
3. English only, no emojis, no shop data (names, NIFs, phones, real ticket numbers).
4. Reference documents describe the code as it is; history goes to `completed_milestones.md`.
5. Never delete milestone entries; old ones move to the archive.
6. Follow `/coding-guidelines` for code.

## Business rules to know

- A ticket (recibo) is one client visit: N garment rows in `ingresos` sharing one `n_recibo`.
- Services: "Limp." (cleaning) and "Plan." (ironing), each with a price per garment. `size` multiplies the unit price (rugs by m2).
- Quarter lock: a row whose date is in a closed quarter cannot be added or changed. The closes are in `accounting_closures`.
- A paid ticket is an invoice sent to AEAT at once: ticket save, payment in `PayDialog` / `RecogPrendas`, `AddGarment`, and the Anular / Rectificar dialogs. Its state is `verifactu_estado` ([architecture.md](architecture.md#database-schema)).
- `ingresos` rows are not edited or deleted from the list view; changes go through the pickup panel and the cancel, void and rectify dialogs so AEAT, the hash chain and the quarter lock stay in step.
- The sales invoice is built from the `ingresos` rows. The Facturas form is the supplier-invoice form and stores in `gastos`.
