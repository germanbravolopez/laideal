# /tackle-issue — Resolve an Open Issue from the Progress Tracker

End-to-end workflow for picking up an open item from `docs/progress_tracker.md` and shipping it. Encapsulates the loop we run for every fix: read → plan (if non-trivial) → implement → update docs → commit.

Use when the user says things like "tackle X", "let's fix Y", "move on to Z" where X/Y/Z is an item in the Blocking Issues or Open Non-Blocking Issues section of `docs/progress_tracker.md`.

---

## Steps

### 1. Locate the issue

- Read `docs/progress_tracker.md` and find the item the user named. It should be in **Blocking Issues** or **Open Non-Blocking Issues** — anything else is already done.
- The full issue text contains the problem statement, sometimes a proposed fix, and the affected files. **Read it in full before touching code** — the description usually answers the design questions you would otherwise ask.

### 2. Decide on the approach

- **Small / mechanical** (rename, find-and-replace, single-file edit): just do it.
- **Medium** (one module, clear intent): state the plan in 1–3 sentences before implementing, but don't block on confirmation.
- **Large or with open design choices** (multiple modules, async refactors, schema changes, UX-affecting changes): use `AskUserQuestion` to surface the choices first, OR write out the plan and ask for go-ahead. Don't sprawl into a refactor that the user didn't agree to.

If the issue lists two approaches (e.g. "short option" vs "architectural option"), pick the user's stated preference. If unstated, default to the simpler one and call out the tradeoff.

### 3. Implement

- **Apply `/coding-guidelines` to every new identifier you introduce.** Read the skill file at the start of implementation if you haven't already this session, and at minimum check the language rule (English names + comments for all code; Spanish only for user-facing UI strings). When the legal or business term is Spanish (e.g. `Huella`, `Recibo`, `Factura`), still name code identifiers with the English equivalent (`rawHash`, `receipt`, `invoice`) and mention the Spanish term in a comment if it aids tracing back to the regulation.
- Read existing call sites before changing signatures — `Grep` first, edit second.
- Track multi-step work with `TodoWrite`. Mark steps complete as you finish them.
- Don't bundle unrelated cleanup into the fix unless the user asked. Dead code touched by the refactor is fair game; dead code in untouched modules is a separate task.
- The user verifies builds in Qt Creator manually — there is no automated build in the local environment. Be precise about syntax. **Never claim "compiles" — say "build needs verification".**

### 4. Update docs (`/update-docs` checklist)

Always update these, in order:

- **`docs/progress_tracker.md`** —
  - Remove the entry from Blocking / Open Non-Blocking.
  - Add a milestone entry under **Pre-release bug fixes — <Month Year>** (or the current section) at the top. Format: `- [x] **Title**: what was done, why, key files. Include rationale that wasn't obvious from the diff.`
  - Don't paste the original issue text — write fresh prose that reflects the actual fix.
- **`docs/architecture.md`** if the structure or data model changed (new module, removed module, signal/slot wiring, DB schema).
- **`docs/modules/<name>.md`** or `docs/modules/<name>/README.md` if the module's public API or behavior changed.
- **`docs/INDEX.md`** if files were added/renamed/removed or new concepts introduced.
- **`docs/README.md`** if new doc files were added.
- **`README.md`** if it requires any update.

Do NOT update `docs/AI_agent_instructions.md` unless the fix changes the agent's onboarding picture (critical-issues table, file map, agent rules).

Skip updates that have nothing to do with the fix. Don't pad the diff.

### 5. Build project

Execute the commands in the build step from `README.md` to verify that the changes are not breaking the project.

### 6. Commit

Single-line commit message in the project's style (see `git log` recent commits). No `Co-Authored-By` line. Examples that work:
- `Verifactu: fix testConnection (POST to /Create) and wire it to 'Probar conexion' button`
- `RecogPrendas: keep pb_payment disabled to prevent Verifactu duplicate-InvoiceID`
- `replace em-dash with ASCII '-' across all source files`

After the commit:
- Run `git status` to confirm a clean tree.
- Output the commit hash and short summary.

---

## When NOT to use this skill

- The user asks a question (no fix implied).
- The work spans multiple unrelated issues — handle one at a time.
- The "issue" is really a design discussion that hasn't crystallized into the tracker yet.

---

## Memory of past fixes

When the user says something like "I just want a single doc update", do not run the full workflow. The skill is the *default* shape for "fix issue X" — not a mandatory template. Use judgment.
