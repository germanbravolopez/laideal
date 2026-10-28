# AI Agent Instructions — La Ideal

**Read this first. This file is your briefing.** Everything critical fits below. References lead you to detail when needed.

---

## Project in One Paragraph

La Ideal is a **Windows desktop laundry management app** — C++17 + Qt 6.4.3 (MinGW), SQLite database, CMake build system. It manages garments, clients, receipts, accounting, invoices, and printing. From 8.0 it integrates **Verifactu** — mandatory AEAT digital invoicing for Spanish businesses. The DB path is a setting (`database.path` in `~/.laideal_settings.json`, `AppSettings`).

## Critical Issues — Check Before Touching Anything

No blocking critical issues. See `docs/progress_tracker.md` for the full issue list.

## Build & Run

- **Requirements**: CMake 3.5+, Qt 6.4.3 (MinGW 11.2), Ninja, C++17 (Windows only)
- **Qt modules needed**: `Widgets`, `Sql`, `PrintSupport`, `Network`
- Open `CMakeLists.txt` in Qt Creator, or, in PowerShell with the toolchain on `PATH` (see `README.md` §Build): `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build`
- Release is automated as the second stage of CI: merging the release PR (`develop` -> `master`) runs `.github/workflows/ci.yml`'s `build` job (configure + build + `ctest`) and then its `release` job (`needs: build`), which reuses the built exe, runs `releases\package.ps1` (windeployqt + zip + Inno Setup installer) and publishes the GitHub Release (tagging `X.Y` itself; on a PR to `master` it is a dry run). `releases\release.ps1 <version>` runs the same pipeline locally (offline fallback, no publish). See `CONTRIBUTING.md` "Release procedure"

## File Map — Where to Find What

| What you need | Where |
|---------------|-------|
| Entry point | `src/app/main.cpp` |
| Debug log file | `src/logging/applogger.h` / `.cpp` |
| App settings (all configurable values) | `src/appsettings/appsettings.h` / `.cpp` |
| Settings dialog (UI to edit settings) | `src/appsettings/settingsdialog.h` / `.cpp` |
| Main window logic | `src/app/mainwindow.h` / `.cpp` |
| Invoice cancellation dialog | `src/app/cancelinvoicedialog.h` / `.cpp` |
| Invoice rectification dialog (R1-R5) | `src/app/rectifyinvoicedialog.h` / `.cpp` |
| Startup recovery of PENDIENTE rows / refused invoices | `src/app/pendingsubmitsdialog.*`, `src/app/refusedinvoicesdialog.*` |
| AEAT records export (XML) | `src/app/aeatexport.*` + `src/app/aeatexportdialog.*` |
| Post-update self-check (`--self-check`) | `src/app/selfcheck.*` + `src/app/selfcheckdialog.*` |
| One copy per database | `src/app/instancelock.*` |
| Ticket save flow | `src/app/mainwindow.cpp` (`saveTicket`) |
| All DB free functions | `src/sql_lite/sql_lite.h` / `.cpp` |
| Generic list viewer | `src/listado/listado.h` / `.cpp` |
| Garment pickup panel, payment and void dialogs | `src/recog_prendas/recog_prendas.*`, `pay_dialog.*`, `voidgarmentsdialog.*` |
| Automatic / manual DB backups | `src/backup/backup_manager.h` / `.cpp` |
| Shared dialog style | `src/uikit/` |
| Shared PDF report scaffolding | `src/reporthtml/` |
| Table view utilities (TableView, FilterWidget, MySortFilterProxyModel, delegates) | `src/tableview/` — single `tableview` CMake library |
| Formal invoices form | `src/facturas/facturas.h` / `.cpp` |
| Accounting reports | `src/contabilidad/contabilidad.h` / `.cpp` |
| Receipt/invoice print orchestration | `src/imprimir/imprimir.h` / `.cpp` |
| ESC/POS printing core (builder, renderer, RAW spooler, optional status API) | `src/printing/` — `escposbuilder`, `ticketrenderer`, `thermalprinter`, `printerstatus`, `statusapiprinter` |
| Add garments to ticket | `src/add_garment/add_garment.h` / `.cpp` |
| Verifactu facade | `src/verifactu/verifactuintegration.h` / `.cpp` |
| Verifactu gateway REST manager | `src/verifactu/verifactumanager.h` / `.cpp` |
| Verifactu backend interface, `VerifactuEstado` and `VerifactuResult` | `src/verifactu/verifactubackend.h`, `src/verifactu/verifactutypes.h` |
| Direct AEAT client | `src/verifactu/aeat*.h` / `.cpp`: `aeatdirectbackend` (flow), `aeatstore` (`aeat_*` tables), `aeatrecord`, `aeathash`, `aeatqr`, `aeatcertificate`, `aeattransport`, `aeatresponse`, `aeatchaincheck`, `aeatselftest`; `producerdeclaration` |
| Verifactu config | `src/verifactu/verifactuconfig.h` / `.cpp` |
| Verifactu invoice model | `src/verifactu/verifactuinvoice.h` / `.cpp` |
| In-app updater (Ayuda → Buscar actualizaciones / startup check) | `src/updater/updater.h` / `.cpp` + `src/updater/updaterdialog.h` / `.cpp` |
| Build config (root) | `CMakeLists.txt` |

## Documentation Map

| Document | Purpose |
|----------|---------|
| `README.md` | Repository root readme files with the project description and initial overview |
| `docs/INDEX.md` | **Quick-find**: every doc, source file, concept |
| `docs/architecture.md` | Module details, DB schema, data flow, dependencies |
| `docs/progress_tracker.md` | Current status, blocking / open issues, backlog |
| `docs/completed_milestones.md` | Finished work, newest first, plus the Archive |
| `docs/testing/` | Testing: how to run, conventions and patterns (`README.md`), every suite (`unit_suites.md`), the end-to-end bench with the fake Verifactu and fake AEAT servers (`e2e.md`), the UI smoke test (`ui_smoke.md`), the manual pre-release checklist (`smoke_test.md`) |
| `docs/ai_agent_instructions.md` | This file |
| `docs/modules/verifactu/` | Full Verifactu implementation docs (gateway and direct AEAT client) |
| `docs/modules/*.md` | Per-module reference docs (MainWindow, sql_lite, listado, recog_prendas, etc.) |

## Skills — Read Fully When Relevant

Project skills live in `.claude/skills/<name>/SKILL.md`. Claude auto-loads one when its `description` matches the task; the user can also type `/name`.

| Skill | Invoke | When to use |
|-------|--------|-------------|
| Tackle an open issue | `/tackle-issue` | When the user names an item from `progress_tracker.md` and wants it fixed end-to-end (plan + implement + docs + commit) |
| Debug a reported bug | `/debugging` | When something is broken — reproduce, localize, reduce, fix root cause, guard with a test |
| Update documentation | `/update-docs` | After any feature, fix, or new insight |
| Create/update skills | `/update-skills` | When a new repeatable workflow is found |
| Coding guidelines | `/coding-guidelines` | Before writing any new code — language, naming, Qt, DB rules |
| Ship a release | `/release X.Y` | Manual only (`disable-model-invocation`) — PR `develop` -> `master`, the merge publishes |
| Security review | `/security-review` | Before release or when touching auth/data |
| Code review | `/code-review` | Before merging significant changes (built-in) |
| Simplify code | `/simplify` | After implementation, to improve quality |

**Rule**: When a skill is relevant to your current task, **read the full skill file before starting work**.

## Subagents

Each agent runs on the model tier set in its `model:` frontmatter, following the orchestration rules in `CLAUDE.md` (Model orchestration): `haiku` for mechanical work, `sonnet` for bounded implementation / tests / audits, `opus` for legal review and the final review gate.

| Agent | Model | File | When to use |
|-------|-------|------|-------------|
| `dead-code-finder` | `haiku` | `.claude/agents/dead-code-finder.md` | Audit `src/` for unused methods (release prep, pre-refactor triage). It reports back; remove what it finds or file it in the tracker - no report file is kept. Invoke via `Agent` tool with `subagent_type: "dead-code-finder"`. |
| `test-engineer` | `sonnet` | `.claude/agents/test-engineer.md` | Write/extend Qt Test coverage for a change or module, proving each test fails without the fix. |
| `verifactu-compliance-auditor` | `opus` | `.claude/agents/verifactu-compliance-auditor.md` | Read-only legal review of any diff touching invoices, `verifactu_*` columns, estado, numbering or the printed QR — run before committing such changes. |
| `guidelines-auditor` | `sonnet` | `.claude/agents/guidelines-auditor.md` | Run the coding-guidelines audit in `.claude/skills/coding-guidelines/audit.md` (before a release / after large work). |

## Agent Obligations

1. **Run `/update-docs`** (or follow its steps manually) after every meaningful change.
2. **Update `docs/progress_tracker.md`** (status, open issues) **and `docs/completed_milestones.md`** (what you did, at the top).
3. **No duplication** — if similar content exists in two docs, consolidate and link.
4. **Document size limits**: see `/update-docs` skill for thresholds and split rules.
5. **English only** in all documentation.
6. **Preserve history** — never delete milestone entries; move old ones to the Archive in `docs/completed_milestones.md`.
7. **Avoid emojis** — do not use emojis in titles or other places
8. **Consider `/coding-guidelines`** for code updates or creation.

## Key Business Logic

- A **ticket (recibo)** = one client visit. N garment rows in `ingresos`, all sharing the same `n_recibo`.
- **Services**: "Limp." (limpieza/cleaning) and "Plan." (plancha/ironing) — each garment has a price per service.
- **Size**: `size` column multiplies unit price (e.g., rugs measured in m²).
- **Accounting lock**: once a quarter is locked, `ingresos` rows for that quarter cannot be added/modified.
- **Verifactu call** happens when a paid invoice event is stored: ticket save (`on_pb_save_clicked()` → `verifactuSubmitInvoice()`), payment in `PayDialog` / `RecogPrendas`, `AddGarment`, and the Anular / Rectificar dialogs.
- **Hash**: each `ingresos` row has a 16-char hash (`genHash16()`) for deduplication.
- **Formal invoice (factura)**: separate from receipt. The sales invoice is built from the `ingresos` rows; the Facturas form is the supplier-invoice form and stores in `gastos`.
