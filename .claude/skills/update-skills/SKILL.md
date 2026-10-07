---
name: update-skills
description: How to create or update a Claude Code skill (.claude/skills/<name>/SKILL.md) or subagent (.claude/agents/<name>.md) in this repo - layout, frontmatter, invocation control, registration in the docs. Use when a repeatable workflow is worth standardising or an existing skill/agent needs changes.
---

# /update-skills — Create or Update Agent Skills and Subagents

Skills are repeatable workflows; subagents are specialist workers with their own context window. Create or update one when you identify a workflow worth standardising for future agents. Official reference: <https://code.claude.com/docs/en/skills> and <https://code.claude.com/docs/en/sub-agents>.

## Layout

```
.claude/
├── skills/
│   └── <skill-name>/
│       ├── SKILL.md          # required: frontmatter + instructions (directory name = /skill-name)
│       ├── reference.md      # optional: detail loaded only when SKILL.md points to it
│       └── scripts/          # optional: helpers, referenced via ${CLAUDE_SKILL_DIR}
└── agents/
    └── <agent-name>.md       # one file per subagent
```

`.claude/commands/` is the legacy format — do not add files there; the project migrated everything to `.claude/skills/`.

## Skill or subagent?

| Need | Use |
|------|-----|
| Steps the main agent follows in the current conversation (checklists, workflows, rules) | Skill |
| A self-contained job whose verbose output should not flood the main context (audits, scans), or that needs a restricted tool set | Subagent |

## When to create a new one

- You performed a multi-step task you'll likely do again (deploy, DB migration, testing flow)
- A workflow is error-prone enough to deserve a step-by-step checklist
- A feature area is complex enough that a standard approach saves future agents time

## SKILL.md format

```markdown
---
name: skill-name
description: What it does + when to use it, trigger phrases first. Claude reads this on every turn to decide whether to auto-invoke the skill.
argument-hint: "<what /skill-name expects>"     # optional
disable-model-invocation: true                   # optional: only the user can trigger it
paths: "src/**, tests/**"                        # optional: only auto-activate when these files are involved
---

# /skill-name — Short Title

One sentence: what this does and when to use it.

## Steps

1. Step one
2. Step two

## Verification

How to confirm the skill succeeded.

## Notes / edge cases

Anything non-obvious, common failure modes, or follow-up actions.
```

### Invocation control — choose deliberately

- **Default** (no flag): the user can type `/skill-name` **and** Claude auto-invokes it when the `description` matches the request. The description is the only thing Claude sees until the skill is loaded, so it decides whether auto-triggering works. Keep it under ~1,500 characters, put the use case first, and include the phrases a user would say.
- **`disable-model-invocation: true`**: manual only. Use for workflows with outward-facing or hard-to-undo side effects (`/release` merges to `master`, pushes a tag and publishes).
- **`user-invocable: false`**: hidden from the `/` menu, Claude-only. Use for background knowledge the user would never type as a command.
- **`paths`**: limits auto-activation to work touching those globs (e.g. `/coding-guidelines` on `src/**`).
- User arguments arrive as `$ARGUMENTS` (or are appended as `ARGUMENTS: ...` when the body has no placeholder).

## Subagent format (`.claude/agents/<name>.md`)

```markdown
---
name: agent-name
description: What it does and when to delegate to it. Add "Use proactively ..." to encourage automatic delegation.
tools: Glob, Grep, Read          # allowlist; omit to inherit all tools
model: sonnet                    # optional
---

System prompt for the subagent: role, job, exclusions, output format.
```

Invoke with the `Agent` tool (`subagent_type: "<name>"`) or `@agent-<name>` in chat.

## Creating a new skill or subagent

1. Create `.claude/skills/<skill-name>/SKILL.md` (or `.claude/agents/<agent-name>.md`) with the frontmatter above
2. Put the most important steps in the first 100 lines; keep SKILL.md under ~500 lines and move reference material into sibling files that SKILL.md links to
3. Keep it focused on one workflow — no omnibus skills
4. Register it in two places:
   - `docs/INDEX.md` — Skills / Subagents table
   - `docs/ai_agent_instructions.md` — Skills / Subagents section
5. Log the creation in `docs/progress_tracker.md`

## Updating an existing skill or subagent

1. Read the file first (always)
2. Edit in place — do not create a duplicate
3. If the trigger conditions changed, update the `description` too — a stale description makes Claude miss (or misfire) the skill
4. Log the update in `docs/progress_tracker.md`

## Existing skills and subagents

| Name | File | Invocation | Purpose |
|------|------|------------|---------|
| `/tackle-issue` | `.claude/skills/tackle-issue/SKILL.md` | user + auto | Resolve a `progress_tracker.md` item end-to-end |
| `/update-docs` | `.claude/skills/update-docs/SKILL.md` | user + auto | Update docs after any change |
| `/update-skills` | `.claude/skills/update-skills/SKILL.md` | user + auto | This file |
| `/coding-guidelines` | `.claude/skills/coding-guidelines/SKILL.md` | user + auto (`src/**`, `tests/**`, `CMakeLists.txt`) | Language, naming, Qt, DB, and safety rules for all new code |
| `/release` | `.claude/skills/release/SKILL.md` | user only | Ship release X.Y end-to-end |
| `dead-code-finder` | `.claude/agents/dead-code-finder.md` | subagent | Find methods declared in `src/` headers but never called |

## Rules

- **The `description` is the trigger** — write it for the model deciding whether to load the skill, not for a human browsing
- **First 100 lines = most critical content** — put workflow steps first, not background
- **English only**
- **One skill, one workflow** — keep them focused
- **Skills should be safe to run twice** (idempotent) where possible
- **Side-effecting workflows get `disable-model-invocation: true`**
