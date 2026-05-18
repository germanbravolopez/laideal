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
├── modules/
│   └── verifactu/                     (AEAT digital invoicing module)
│       ├── README.md
│       ├── INDEX.md
│       ├── GUIA_PASO_A_PASO.md
│       ├── RESUMEN_IMPLEMENTACION.md
│       └── VERIFACTU_REST_API.md
├── development/
│   └── planning_verifactu.md          (historical planning notes)
└── todo/
    └── hardcoded_paths.png            (screenshot of known hardcoded-paths issue)
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

### Verifactu — AEAT digital invoicing

| Document | Description | Read time |
|----------|-------------|-----------|
| [README.md](./modules/verifactu/README.md) | Overview, quick start, status | 3 min |
| [INDEX.md](./modules/verifactu/INDEX.md) | Navigation map and reading paths | 3 min |
| [GUIA_PASO_A_PASO.md](./modules/verifactu/GUIA_PASO_A_PASO.md) | Complete implementation guide | 45 min |
| [RESUMEN_IMPLEMENTACION.md](./modules/verifactu/RESUMEN_IMPLEMENTACION.md) | Architecture, DB schema SQL, roadmap | 20 min |
| [VERIFACTU_REST_API.md](./modules/verifactu/VERIFACTU_REST_API.md) | REST API complete field reference | 15 min |
| [EJEMPLO_IMPLEMENTACION.cpp](../src/verifactu/EJEMPLO_IMPLEMENTACION.cpp) | 10+ practical code examples | 5–10 min |

## Development Notes

| Document | Description |
|----------|-------------|
| [planning_verifactu.md](./development/planning_verifactu.md) | Historical Verifactu planning notes (raw) |

---

## Adding documentation for a new module

1. Create `docs/modules/<module-name>/` folder
2. Add at minimum a `README.md` with: description, source file list, quick start
3. Register the new doc in the tables above and in `docs/INDEX.md`
