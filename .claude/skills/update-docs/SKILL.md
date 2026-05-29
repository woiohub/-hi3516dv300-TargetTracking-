---
name: update-docs
description: >
  Use after ANY code change, bug fix, feature addition, or configuration modification
  to update project documentation. Checks for README (项目说明), learning doc (项目学习),
  design doc (项目思路), and bugfix doc (项目错误记录). Updates existing docs to match
  current code, creates missing docs if they don't exist. ALWAYS use this skill after
  committing code changes, fixing bugs, adding features, or modifying configs — even
  if the user doesn't explicitly ask for doc updates.
---

# Update Project Documentation

After any project modification, ensure all documentation stays in sync with the code.

## When to trigger

- After any file edit, bug fix, or feature implementation
- After committing code changes
- When the user says "update docs" or "更新文档"
- When the user asks to create project documentation

## Documentation types

The project should have these four documentation files (Chinese naming convention):

| Type | Typical filename | Content |
|------|-----------------|---------|
| 项目说明文档 | `README.md` | Project overview, hardware/software env, build & deploy steps, architecture |
| 项目学习文档 | `doc/learning.md` | Technical deep-dives: algorithms, APIs, data formats, troubleshooting |
| 项目思路文档 | `doc/design.md` | Design decisions, trade-offs, module design, performance considerations |
| 项目错误记录文档 | `doc/bugfix.md` | Bug symptoms, root cause analysis, fixes, lessons learned |

## Process

### Step 1: Identify documentation files

Search the project for existing documentation:

```bash
# Check common locations
ls README.md readme.md doc/*.md docs/*.md 2>/dev/null
```

Match found files to the four documentation types by reading their content. A file named `bugfix_mmz_ko.md` in `doc/` is the bugfix doc even if the name isn't exactly `bugfix.md`.

### Step 2: Understand what changed

Analyze the recent changes:
- Read `git diff` or `git log` to understand what was modified
- Read the changed source files to understand the current state
- Identify which documentation types are affected

### Step 3: Update or create each affected doc

For each documentation type:

**If the file exists:**
- Read the current content
- Identify sections that are now outdated or incomplete
- Update those sections to match the current code
- Add new sections if the change introduced new concepts
- Remove sections that are no longer relevant

**If the file does NOT exist:**
- Create it with appropriate initial content based on the current project state
- Use the table above as a guide for what belongs in each doc

### Step 4: Cross-reference

After updating, verify:
- README.md accurately reflects the current project structure, build commands, and deployment steps
- learning.md covers any new technical concepts introduced
- design.md explains any new design decisions
- bugfix.md documents any new bugs encountered and fixed

## Writing guidelines

- Write in Chinese (matching the project's existing convention)
- Use markdown formatting with clear headers
- Include code blocks for commands, configurations, and code snippets
- Keep README.md concise and practical (user-facing)
- Keep learning.md thorough and educational
- Keep design.md focused on decisions and rationale
- Keep bugfix.md factual with clear root-cause-analysis structure

## Example: After fixing a bug

1. Read the git diff to understand the fix
2. Update `doc/bugfix.md` — add a new section with: symptom, root cause, fix, verification
3. If the fix changed deployment steps, update `README.md`
4. If the fix introduced new technical knowledge, update `doc/learning.md`
5. If the fix involved a design decision, update `doc/design.md`

## Important

- Do NOT create documentation files in the project root unless they're README.md
- Place documentation in `doc/` directory
- Do NOT overwrite documentation wholesale — surgically update the affected sections
- If a doc is already up-to-date, skip it (don't make unnecessary changes)
