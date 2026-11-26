# Documentation - La Ideal

Map of the `docs/` folder. To find a file, class or concept use [INDEX.md](./INDEX.md); the repository entry points are the root [README](../README.md) and [CONTRIBUTING](../CONTRIBUTING.md).

## Project documents

| Document | Content |
|----------|---------|
| [ai_agent_instructions.md](./ai_agent_instructions.md) | Agent briefing: file map, doc map, skills, obligations |
| [architecture.md](./architecture.md) | Modules and dependencies, data flows, database schema |
| [INDEX.md](./INDEX.md) | Where to find any file, concept or topic |
| [progress_tracker.md](./progress_tracker.md) | Current status, open issues, backlog |
| [completed_milestones.md](./completed_milestones.md) | Finished work, newest first |
| [completed_milestones_archive.md](./completed_milestones_archive.md) | Milestones of 10.13 and before |
| [reviews/](./reviews/) | Review reports and plans, kept for reference |

## Testing

| Document | Content |
|----------|---------|
| [testing/README.md](./testing/README.md) | Running the suites, conventions, patterns, adding a suite, CI toolchain |
| [testing/unit_suites.md](./testing/unit_suites.md) | What each unit and integration suite covers |
| [testing/e2e.md](./testing/e2e.md) | End-to-end bench with the fake Verifactu and fake AEAT servers |
| [testing/ui_smoke.md](./testing/ui_smoke.md) | Automated UI smoke test and the AEAT pre-production run |
| [testing/smoke_test.md](./testing/smoke_test.md) | Manual checklist: part 1 before a release, part 2 the shop's post-update check |

## Modules

| Document | Content |
|----------|---------|
| [modules/mainwindow.md](./modules/mainwindow.md) | Main window, menus, save flow, the `src/app/` dialogs |
| [modules/sql_lite.md](./modules/sql_lite.md) | Database function reference |
| [modules/listado.md](./modules/listado.md) | List viewer, insert dialog, list PDF |
| [modules/recog_prendas.md](./modules/recog_prendas.md) | Pickup panel, payment and void dialogs |
| [modules/add_garment.md](./modules/add_garment.md) | Adding garments to a ticket |
| [modules/facturas.md](./modules/facturas.md) | Supplier-invoice form |
| [modules/contabilidad.md](./modules/contabilidad.md) | Accounting reports and quarter close |
| [modules/imprimir.md](./modules/imprimir.md) | Print orchestration and layouts |
| [modules/printing.md](./modules/printing.md) | ESC/POS core: builder, renderer, RAW spooler, Status API |
| [modules/printer/README.md](./modules/printer/README.md) | Printer research dossier (design background) |
| [modules/backup.md](./modules/backup.md) | Automatic and manual SQLite backups |
| [modules/uikit.md](./modules/uikit.md) | Shared dialog style |

### Verifactu

| Document | Content |
|----------|---------|
| [modules/verifactu/README.md](./modules/verifactu/README.md) | Module reference: architecture, API, configuration, schema, integration points, errors |
| [modules/verifactu/verifactu-requirements.md](./modules/verifactu/verifactu-requirements.md) | RD 1007/2023 and Orden HAC/1177/2024 requirements against the code |
| [modules/verifactu/rest_api.md](./modules/verifactu/rest_api.md) | The gateway's REST API as researched (the code is authoritative) |
| [modules/verifactu/aeat-direct-investigation.md](./modules/verifactu/aeat-direct-investigation.md) | Research: direct AEAT client versus the gateway |

## Adding a module document

Create `docs/modules/<name>.md` (a folder for a complex module) with a description, the source files, the key interface and integration notes. Register it in the tables above and in [INDEX.md](./INDEX.md).
