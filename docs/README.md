# Documentation — La Ideal

Navigation index for the `docs/` folder. For the project-wide quick-find reference see `docs/INDEX.md`.

---

## Structure

```
docs/
├── README.md                          (this file — docs folder navigation)
├── INDEX.md                           (project-wide quick-find: files, concepts, topics)
├── AI_agent_instructions.md           (agent onboarding — start here)
├── architecture.md                    (modules, DB schema, data flow, known issues)
├── progress_tracker.md                (what's done, blocking issues, roadmap)
└── modules/
    ├── mainwindow.md                  (MainWindow — central controller)
    ├── sql_lite.md                    (database free-function API)
    ├── listado.md                     (generic list viewer + PDF export)
    ├── recog_prendas.md               (garment pickup panel)
    ├── facturas.md                    (formal supplier invoice form)
    ├── contabilidad.md                (accounting reports and period locking)
    ├── imprimir.md                    (Excel + print generation)
    ├── add_garment.md                 (add garments to existing ticket)
    └── verifactu/                     (AEAT digital invoicing module)
        ├── README.md
        ├── step_by_step_guide.md
        ├── implementation_summary.md
        └── rest_api.md
```

---

## Core Documentation

| Document | Purpose | Read time |
|----------|---------|-----------|
| [AI_agent_instructions.md](./AI_agent_instructions.md) | Agent onboarding, critical issues, file map, rules | 5 min |
| [architecture.md](./architecture.md) | Full module reference, DB schema, data flow | 15 min |
| [progress_tracker.md](./progress_tracker.md) | Blocking issues, in-progress work, history | 5 min |
| [INDEX.md](./INDEX.md) | Quick-find: every file, concept, topic | As needed |

## Module Documentation

| Document | Description |
|----------|-------------|
| [modules/mainwindow.md](./modules/mainwindow.md) | MainWindow methods, save flow, column indices |
| [modules/sql_lite.md](./modules/sql_lite.md) | DB API function reference and usage patterns |
| [modules/listado.md](./modules/listado.md) | List viewer, InsertNewItem, GenListado |
| [modules/recog_prendas.md](./modules/recog_prendas.md) | Garment pickup operations and table indices |
| [modules/facturas.md](./modules/facturas.md) | Supplier invoice form fields and validation |
| [modules/contabilidad.md](./modules/contabilidad.md) | Accounting report modes and period locking |
| [modules/imprimir.md](./modules/imprimir.md) | Excel/print flow and layout modes |
| [modules/add_garment.md](./modules/add_garment.md) | Add-garment workflow and validation |

### Verifactu — AEAT digital invoicing

| Document | Description | Read time |
|----------|-------------|-----------|
| [README.md](./modules/verifactu/README.md) | Overview, architecture, key interface, status | 5 min |
| [step_by_step_guide.md](./modules/verifactu/step_by_step_guide.md) | Complete implementation guide | 45 min |
| [implementation_summary.md](./modules/verifactu/implementation_summary.md) | Class reference, DB schema SQL, security, roadmap | 10 min |
| [rest_api.md](./modules/verifactu/rest_api.md) | REST API complete field reference | 15 min |

---

## Adding documentation for a new module

1. Create `docs/modules/<module-name>.md` (or a subfolder for complex modules)
2. Add at minimum: description, source file list, key interface, integration notes
3. Register the new doc in the tables above and in `docs/INDEX.md`
