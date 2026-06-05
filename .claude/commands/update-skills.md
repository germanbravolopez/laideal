# /update-skills — Create or Update Agent Skills

A skill is a `.md` file in `.claude/commands/` that defines a repeatable workflow. Create or update skills when you identify a workflow worth standardising for future agents.

## When to create a new skill

- You performed a multi-step task you'll likely do again (deploy, DB migration, testing flow)
- A workflow is error-prone enough to deserve a step-by-step checklist
- A feature area is complex enough that a standard approach saves future agents time

## Skill file format

```markdown
# /skill-name — Short Title

One sentence: what this does and when to use it.

## Most critical information goes here (first 100 lines)

[Steps, prerequisites, expected outcome — the essential stuff up front]

## Steps

1. Step one
2. Step two

## Verification

How to confirm the skill succeeded.

## Notes / edge cases

Anything non-obvious, common failure modes, or follow-up actions.
```

## Creating a new skill

1. Create `.claude/commands/<skill-name>.md` — file name becomes the slash command (`/skill-name`)
2. Put the most important steps in the first 100 lines
3. Keep the skill focused on one workflow — no omnibus skills
4. Register it in two places:
   - `docs/INDEX.md` — Skills table
   - `docs/ai_agent_instructions.md` — Skills section
5. Log the creation in `docs/progress_tracker.md`

## Updating an existing skill

1. Read the skill file first (always)
2. Add `> Last updated: YYYY-MM-DD` near the top
3. Edit in place — do not create a duplicate
4. Log the update in `docs/progress_tracker.md`

## Existing skills in this project

| Skill | File | Purpose |
|-------|------|---------|
| `/update-docs` | `.claude/commands/update-docs.md` | Update docs after any change |
| `/update-skills` | `.claude/commands/update-skills.md` | This file |
| `/coding-guidelines` | `.claude/commands/coding-guidelines.md` | Language, naming, Qt, DB, and safety rules for all new code |

## Rules

- **First 100 lines = most critical content** — put workflow steps first, not background
- **English only**
- **One skill, one workflow** — keep them focused
- **Skills should be safe to run twice** (idempotent) where possible
- If a skill grows beyond ~100 lines of actual instructions (excluding boilerplate), split it
