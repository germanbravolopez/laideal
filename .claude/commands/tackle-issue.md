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
- Be precise about syntax — step 5 will compile the change, but a clean build is much faster to reach if the code is right the first time.

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

Verify the change compiles before committing. From the repo root in PowerShell:

```powershell
cmake --build build
```

This is the incremental build — the `build/` directory is already configured from a prior `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release` run (see README §Build for the full first-time setup). Pipe to `tail -40` or `Select-Object -Last 40` to keep the output from flooding context; the only line that must be present at the end is `Linking CXX executable src\app\laideal.exe` (or the static-lib equivalent for the modules you touched).

If the build fails:
- Compiler errors → fix them and re-run; don't proceed to step 6.
- `Permission denied` linking `laideal.exe` → the running app is holding the file. Tell the user to close it, then re-run.
- PATH errors (cmake/ninja/qt not found) → the shell doesn't have the toolchain on PATH. Prepend the line from README §Build that sets `$env:PATH`, then re-run.

A clean build is a hard prerequisite for step 6 — never commit a fix that didn't link.

### 6. Commit

- **Single-line subject only** — no body, no `Co-Authored-By` line. "Single-line" describes the *shape* (one paragraph, no separate body), not the length: subjects are routinely 300–600 characters when the fix needs that much context. Match the verbosity of recent commits in `git log` — terse 60-char subjects are *not* the project style. Examples:
  - Long (typical for multi-file fixes): `sql_lite: collapse updateTicketVerifactuFields + updateTicketVerifactuFieldsForSeq into one seq-scoped helper, and count paid rows in nextVerifactuInvoiceSeq so a local-only PayDialog event (Verifactu disabled / submit failed locally) still increments the next event's seq instead of colliding on seq=0`
  - Short (only when the fix really is mechanical): `replace em-dash with ASCII '-' across all source files`
- **Branch**: commit on `develop` (or a feature branch). Never commit to `master` — `master` only fast-forwards at release time.
- **Stage only the files this issue touched.** Never `git add -A` / `git add .` — pre-existing unrelated edits in the working tree must not ride along. If you find such edits, leave them alone and call them out in your final message.
- **Never `--amend`.** If the pre-commit hook rejects the commit, fix the issue and create a **new** commit — `--amend` after a hook-rejected commit would overwrite the *previous* commit (the rejected one never landed).

After the commit:
- Run `git status` to confirm no remaining modifications to the files this issue touched. Pre-existing unrelated modifications still showing as `M` are fine — flag them in your final message but don't fold them in.
- Output the commit hash and short summary.

---

## When NOT to use this skill

- The user asks a question (no fix implied).
- The work spans multiple unrelated issues — handle one at a time.
- The "issue" is really a design discussion that hasn't crystallized into the tracker yet.

---

## Memory of past fixes

When the user says something like "I just want a single doc update", do not run the full workflow. The skill is the *default* shape for "fix issue X" — not a mandatory template. Use judgment.
