# /update-docs — Update Project Documentation

Run this after every feature implementation, bug fix, refactor, or whenever you gain a new insight about the project. Good documentation saves the next agent time.

## What to update (checklist, in order of importance)

### 1. `docs/progress_tracker.md` — always update this
- Add an entry at the **top** of "In Progress" or "Completed Milestones"
- Format: `- [x] What you did — which file/module — date`
- **Milestone section header must track the latest *released* version.** Completed Milestones is grouped into `### Post-<X.Y> development — <Month Year>` sections, where `X.Y` is the release that had **already shipped** when that batch of work was done (the work becomes the *next* release). Before adding your entry, read the "Latest release" version in **Current Status**: if it is newer than the version in the top milestone section's header, **create a new `### Post-<latest-release> development — <Month Year>` section** at the top of Completed Milestones and add your entry there — do **not** append to the now-stale section. Only append to the existing top section when no release has shipped since it was created (i.e. its `X.Y` already equals the latest release). This prevents post-9.2 and post-9.3 work from piling up under a stale "Post-9.1 development" heading.
- If a blocking issue was fixed, move it out of "Blocking Issues"
- If a task is newly discovered, add it to the appropriate section
- **Keep "Open Non-Blocking Issues" a real to-do list.** If an item is consciously parked because the effort outweighs the value (or it waits on an external trigger), put it in the **Backlog (deferred — low value / not currently planned)** table, not in Open. Don't delete it — parking ≠ done.
- Entries older than ~6 months with no further action: move to Archive
- **Current Status → "Latest release" paragraph**: describe **only the latest release** — what it ships, its headline changes, and its tag link. Do **not** summarise or link prior releases in this paragraph (no "Previous: X.Y — …" trailer). The full release history already lives in `releases_notes.txt` (and the Completed Milestones below); the reader can consult it for anything before the current version. Keep this paragraph a single-release snapshot so it does not grow every cycle.

### 2. `docs/AI_agent_instructions.md` — update when big-picture changed
- Update File Map if you added, renamed, or removed files
- Update Critical Issues table (mark fixed, add new)
- Keep the file under ~150 lines — move anything non-critical to `architecture.md`
- The first 100 lines must remain the essential agent briefing

### 3. `docs/architecture.md` — update when structure or data model changed
- Update module overview or module details for any changed module
- Update DB schema if you ran ALTER TABLE or created new tables
- Update Known Issues: mark fixed items, add new ones
- Update data flow if the save or print flow changed
- Keep under ~350 lines — if a module section grows large, move it to `docs/modules/<name>/`

### 4. `docs/INDEX.md` — update when files or concepts were added
- Add new source files to the Source Files table
- Add new docs to the Documentation Files table
- Add new concepts or terms to the Glossary
- Add topic→file entries for new features

### 5. `docs/modules/<name>/` — update when working on a specific module
- If the module has a dedicated docs folder, update its `README.md`
- For Verifactu: update `docs/modules/verifactu/RESUMEN_IMPLEMENTACION.md` if architecture changed
- If creating a new module folder, follow the structure in `docs/modules/verifactu/`

### 6. `docs/README.md` — update when new doc files are added
- Add the new file to the navigation table under the right section

### 7. `README.md` (repo root) — update when user-visible behaviour, build, or workflow changed
- Update the Features list if a user-visible feature was added/removed/renamed
- Update Requirements / Build / Configuration if the toolchain, build commands, or settings keys changed
- Update Development workflow or Release procedure if the branching, merge, tag, or release-pipeline steps changed
- Skip if the change is purely internal (refactor, docs-only, bug fix with no behaviour change)

### 8. `releases_notes.txt` (repo root) — update when shipping a release
- Only edit as part of a `release X.Y` commit on the working branch (see Development workflow in README)
- Add a new `X.Y` section at the **top** with customer-facing changes in plain language (English, matching the existing entries — Inno Setup shows this file to end users at install time, and the same content is reused as the GitHub release body)
- Do **not** add an entry for in-progress work; the entry lands in the same commit that bumps `CMakeLists.txt` to the new version

---

## Document size limits

| File | Soft limit | What to do when exceeded |
|------|-----------|--------------------------|
| `docs/AI_agent_instructions.md` | 150 lines | Move details to `architecture.md` |
| `docs/architecture.md` | 350 lines | Move module section to `docs/modules/<name>/architecture.md` |
| `docs/progress_tracker.md` | 250 lines | Move older milestones to Archive section |
| `docs/INDEX.md` | 200 lines | Split into `docs/INDEX-<domain>.md` |
| Any module doc (`docs/modules/`) | 400 lines per file | Split by topic (API ref, implementation guide, examples) |

---

## Rules

- **English only** in all documentation
- **No duplication** — if content exists elsewhere, link to it instead of copying
- **Most important content in the first 100 lines** of any document
- **Never delete** progress tracker entries — move them to Archive
- **Preserve insights** — if you discovered something non-obvious about the codebase, document it
- When adding a new doc file, register it in `docs/INDEX.md` and `docs/README.md`
- Skills live in `.claude/commands/` — register new skills in `docs/INDEX.md`
- **Avoid emojis** — do not use emojis in titles or other places
