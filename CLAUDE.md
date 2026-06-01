# La Ideal — Claude Code Instructions

**Before starting any task**, read `docs/AI_agent_instructions.md` in full. It contains the project briefing, critical issues, file map, and your obligations as an agent.

Quick links:
- `docs/AI_agent_instructions.md` — start here (file map, critical issues, business logic)
- `docs/INDEX.md` — find any file, concept, or topic
- `docs/architecture.md` — module details, DB schema, data flow
- `docs/progress_tracker.md` — what's in progress and what's blocked
- `.claude/commands/` — available slash commands (`/update-docs`, `/update-skills`, `/coding-guidelines`, `/tackle-issue`)

After completing any task, build the code in PowerShell to verify the application is still created fine and run `/update-docs` or follow its checklist to keep the documentation current.

## Shell preference

Default to the **Bash** tool for every shell command — `git`, `ls`, `cat`, `grep`-style work, file inspection, etc. Bash on this machine handles those faster and pipes UTF-8 cleanly.

Use the **PowerShell** tool **only** for commands that touch the build toolchain (so the Qt + MinGW + Ninja `PATH` setup in `README.md` §Build applies natively):
- `cmake -B build ...` / `cmake --build build` (Release)
- `cmake -B build-debug ...` / `cmake --build build-debug` (Debug, VSCode F5)
- `release.ps1` invocations

Everything else — including `git commit`, `git diff`, `git log`, `git push`, `git status` — runs in Bash.
