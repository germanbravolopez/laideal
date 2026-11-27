# La Ideal — Claude Code Instructions

**Before starting any task**, read `docs/ai_agent_instructions.md` in full. It contains the project briefing, critical issues, file map, and your obligations as an agent.

Quick links:
- `docs/ai_agent_instructions.md` — start here (file map, critical issues, business logic)
- `docs/INDEX.md` — find any file, concept, or topic
- `docs/architecture.md` — module details, DB schema, data flow
- `docs/progress_tracker.md` — current status, open issues and backlog
- `docs/completed_milestones.md` — finished work history (newest first); older sections in `docs/completed_milestones_archive.md`
- `docs/testing/README.md` — the test suites, how to run them, the patterns to follow
- `CONTRIBUTING.md` — development workflow, branching, release procedure
- `.claude/skills/<name>/SKILL.md` — project skills (`/tackle-issue`, `/debugging`, `/update-docs`, `/update-skills`, `/coding-guidelines`, `/release`); Claude auto-invokes them from their `description` frontmatter, except `/release` (manual only)
- `.claude/agents/` — project subagents (`test-engineer`, `verifactu-compliance-auditor`, `guidelines-auditor`, `dead-code-finder`)

After completing any task, run `.\make.ps1 test` in PowerShell (it builds the app and runs every test suite) and require it green, then run `/update-docs` or follow its checklist to keep the documentation current.

## Standing rules

- **Commits**: one line, at most 300 characters, no body, no `Co-Authored-By`. Stage only the files of the task; never `--amend`.
- **No Claude attribution** anywhere: not in commits, pull requests, comments or anything else posted on GitHub.
- **Branching**: work on `develop`, or on a short-lived branch cut from it (`feature/`, `fix/`, `docs/`, `tests/`, `ci/`) and merged back by pull request. `master` moves only through the release pull request (merging it publishes the release); a fix to the released version goes on `hotfix/X.Y` cut from `master`. Never commit or push to `master`, never merge with `--admin`, never delete `develop`.
- **Merging a pull request** needs the owner's explicit word for that pull request; a go-ahead for one does not cover the next.
- **No shop data in the repository**: no real customers, tickets, certificates, settings files or databases, in code, tests, docs or commit messages.
- **Never reach the real AEAT.** Tests and checks use the fake servers only (`.\make.ps1 aeat-dry`). `.\make.ps1 aeat-real` is run by the owner, never by an agent.
- **The UI smoke test** (`.\make.ps1 ui-smoke`) takes over the mouse and keyboard: warn the owner before starting it.
- **Legal or product decisions** are the owner's: ask, and keep working on what does not depend on the answer.
- **Docs** are written in English, without emojis.

## Model orchestration

The main session is the **orchestrator and reviewer**: it plans, takes design decisions, writes the hard parts and signs off before anything is called done. Self-contained work below that level is delegated to a subagent on a lighter model. These instructions are the user's standing request to delegate; they override any default to avoid spawning subagents.

### Pick the tier by the task, not by the prompt

Split a prompt into its tasks and give each one the cheapest tier that does it reliably:

| Tier | Model (`Agent` `model:`) | Use for | Examples in this repo |
|------|--------------------------|---------|-----------------------|
| **Light** | `haiku` | Mechanical, repetitive, fully specified; the result is easy to check | Find files / call sites / where a string or object name is used (`Explore`); list what a doc or test file covers; apply the same small edit across many files to a given pattern; run the build or `ctest` and report only failures; tally counts for `docs/INDEX.md`; the dead-code scan |
| **Standard** | `sonnet` | Clear intent, one module, bounded judgement | Implement a change the orchestrator has already designed; write or extend Qt Test / e2e / `tests/ui` scenarios (`test-engineer`); draft the `/update-docs` edits (milestone, tracker, INDEX, module doc); reproduce and localize a bug (`/debugging` steps 1-3); the coding-guidelines audit |
| **Advanced** | `opus` (or the main session itself) | Design choices, cross-module or async changes, anything legal or irreversible, and every final review | Plans for tracker items with open choices; anything touching invoices, `verifactu_*` columns, estado, `n_recibo` numbering, the hash chain or the printed QR; schema migrations; release decisions; the review gate below; `verifactu-compliance-auditor` (run only during `/release`, see the gate) |

When unsure between two tiers, pick the higher one for **code that ships** and the lower one for **read-only searches** (a weak search result is cheap to redo; a wrong edit is not).

### How to delegate

- **Do it inline when it is small.** A subagent starts cold and re-reads context, so a one-file edit, a single grep or a two-line doc change is cheaper done directly. Delegate when the task is self-contained and big enough to pay for that (many files, long output to digest, a whole test suite to write), or when several such tasks can run **in parallel** in one message.
- **Brief like a colleague who has not seen the conversation**: goal, exact files / identifiers, constraints (`/coding-guidelines`, English identifiers, no shop data, never reach the real AEAT), what *not* to touch, and the expected output format (e.g. "report findings as `file:line - problem`, do not edit"). Ask for a short report, not file dumps.
- **Set `model` explicitly** on every `Agent` call for built-in types (`Explore`, `general-purpose`); the project subagents in `.claude/agents/` carry their tier in their `model:` frontmatter.
- **One writer per file.** Parallel subagents may read anything but must not edit the same files; split the work by file or module.
- **Subagent output is a draft.** Read what it changed (`git diff`) or verify its claims before relaying them or building on them. Its report is not shown to the user; summarise what matters.

### Review gate before "done" (never a lighter tier)

Before reporting a task finished, the orchestrator (or an `opus` subagent for large diffs) reviews the whole change against the request:

1. Read the full `git diff`: correctness, edge cases, `/coding-guidelines`, no unrelated edits, no shop data or secrets.
2. **Only during `/release`**: run `verifactu-compliance-auditor` (Opus) on the changes since the last release that touch Verifactu / invoices / numbering / the QR, and resolve every blocking finding. Outside a release the auditor is not run; the orchestrator reviews those changes itself in step 1.
3. `.\make.ps1 test` green (the run itself may be a `haiku` task; the orchestrator reads the result), docs updated per `/update-docs`.
4. Findings are fixed or filed in the tracker right away, never left only in chat.

## Shell preference

Default to the **Bash** tool for every shell command — `git`, `ls`, `cat`, `grep`-style work, file inspection, etc. Bash on this machine handles those faster and pipes UTF-8 cleanly.

Use the **PowerShell** tool **only** for commands that touch the build toolchain:
- `.\make.ps1 <target>` — the normal entry point; it puts the Qt + MinGW + Ninja toolchain on `PATH` itself. `.\make.ps1` alone lists the targets; the usual ones are `build`, `test` (`-Filter <regex>` for some suites), `uitest`, `ui-smoke`, `aeat-dry` and `regression` (everything CI runs).
- Raw `cmake -B <dir> ...` / `cmake --build <dir>` / `ctest --test-dir build` when a `make.ps1` target does not fit (set the `PATH` from `README.md` §Build first). Build folders: `build/` (Release, app and tests), `build-debug/` (VSCode F5), `build-uitest/` (UI-test app and fake servers), `build-release/` (packaging).
- `releases\release.ps1 X.Y` — the local fallback for publishing only; a release is normally published by merging the release pull request (`/release`).

Everything else — including `git commit`, `git diff`, `git log`, `git push`, `git status` — runs in Bash.
