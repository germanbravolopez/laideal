# La Ideal — Claude Code Instructions

**Before starting any task**, read `docs/ai_agent_instructions.md` in full. It contains the project briefing, critical issues, file map, and your obligations as an agent.

Quick links:
- `docs/ai_agent_instructions.md` — start here (file map, critical issues, business logic)
- `docs/INDEX.md` — find any file, concept, or topic
- `docs/architecture.md` — module details, DB schema, data flow
- `docs/progress_tracker.md` — current status and open / blocking issues
- `docs/completed_milestones.md` — finished work history (newest first)
- `.claude/skills/<name>/SKILL.md` — project skills (`/tackle-issue`, `/debugging`, `/update-docs`, `/update-skills`, `/coding-guidelines`, `/release`); Claude auto-invokes them from their `description` frontmatter, except `/release` (manual only)
- `.claude/agents/` — project subagents (`test-engineer`, `verifactu-compliance-auditor`, `guidelines-auditor`, `dead-code-finder`)

After completing any task, build the code in PowerShell to verify the application is still created fine and run `/update-docs` or follow its checklist to keep the documentation current.

## Model orchestration

The main session (Opus 5.5) is the **orchestrator and reviewer**: it plans, takes design decisions, writes the hard parts and signs off before anything is called done. Self-contained work below that level is delegated to a subagent on a lighter model. These instructions are the user's standing request to delegate; they override any default to avoid spawning subagents.

### Pick the tier by the task, not by the prompt

Split a prompt into its tasks and give each one the cheapest tier that does it reliably:

| Tier | Model (`Agent` `model:`) | Use for | Examples in this repo |
|------|--------------------------|---------|-----------------------|
| **Light** | `haiku` | Mechanical, repetitive, fully specified; the result is easy to check | Find files / call sites / where a string or object name is used (`Explore`); list what a doc or test file covers; apply the same small edit across many files to a given pattern; run the build or `ctest` and report only failures; tally counts for `docs/INDEX.md`; the dead-code scan |
| **Standard** | `sonnet` | Clear intent, one module, bounded judgement | Implement a change the orchestrator has already designed; write or extend Qt Test / e2e / `tests/ui` scenarios (`test-engineer`); draft the `/update-docs` edits (milestone, tracker, INDEX, module doc); reproduce and localize a bug (`/debugging` steps 1-3); the coding-guidelines audit |
| **Advanced** | `opus` (or the main session itself) | Design choices, cross-module or async changes, anything legal or irreversible, and every final review | Plans for tracker items with open choices; anything touching invoices, `verifactu_*` columns, estado, `n_recibo` numbering, the hash chain or the printed QR (`verifactu-compliance-auditor`); schema migrations; release decisions; the review gate below |

When unsure between two tiers, pick the higher one for **code that ships** and the lower one for **read-only searches** (a weak search result is cheap to redo; a wrong edit is not).

### How to delegate

- **Do it inline when it is small.** A subagent starts cold and re-reads context, so a one-file edit, a single grep or a two-line doc change is cheaper done directly. Delegate when the task is self-contained and big enough to pay for that (many files, long output to digest, a whole test suite to write), or when several such tasks can run **in parallel** in one message.
- **Brief like a colleague who has not seen the conversation**: goal, exact files / identifiers, constraints (`/coding-guidelines`, English identifiers, no shop data, never reach the real AEAT), what *not* to touch, and the expected output format (e.g. "report findings as `file:line - problem`, do not edit"). Ask for a short report, not file dumps.
- **Set `model` explicitly** on every `Agent` call for built-in types (`Explore`, `general-purpose`); the project subagents in `.claude/agents/` carry their tier in their `model:` frontmatter.
- **One writer per file.** Parallel subagents may read anything but must not edit the same files; split the work by file or module.
- **Subagent output is a draft.** Read what it changed (`git diff`) or verify its claims before relaying them or building on them. Its report is not shown to the user; summarise what matters.

### Review gate before "done" (always Opus)

Before reporting a task finished, the orchestrator (or an `opus` subagent for large diffs) reviews the whole change against the request:

1. Read the full `git diff`: correctness, edge cases, `/coding-guidelines`, no unrelated edits, no shop data or secrets.
2. Changes touching Verifactu / invoices / numbering / the QR: run `verifactu-compliance-auditor` (Opus) and resolve every blocking finding.
3. Build + `ctest` green (the run itself may be a `haiku` task; the orchestrator reads the result), docs updated per `/update-docs`.
4. Findings are fixed or filed in the tracker right away, never left only in chat.

## Shell preference

Default to the **Bash** tool for every shell command — `git`, `ls`, `cat`, `grep`-style work, file inspection, etc. Bash on this machine handles those faster and pipes UTF-8 cleanly.

Use the **PowerShell** tool **only** for commands that touch the build toolchain (so the Qt + MinGW + Ninja `PATH` setup in `README.md` §Build applies natively):
- `cmake -B build ...` / `cmake --build build` (Release)
- `cmake -B build-debug ...` / `cmake --build build-debug` (Debug, VSCode F5)
- `release.ps1` invocations

Everything else — including `git commit`, `git diff`, `git log`, `git push`, `git status` — runs in Bash.
