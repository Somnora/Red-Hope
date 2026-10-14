# Red Hope Agent Instructions

## Project context

Expected repository root: /Volumes/Unreal/red_hope/red_hope
Canonical Obsidian context file: /Users/jamesmcshane/Desktop/obsidian-vault/the-eye/Projects/red_hope/claude-context.md

Before planning or editing, confirm the Git root, read the exact context above, and report both paths. Treat context as provenance, not proof of current behavior. If routing cannot be resolved, report Vault context link failed and stop implementation.

## Working rules

- Follow the repository rules in CLAUDE.md (repo location, build and drive, hard rules, secrets). They apply to every agent, not only Claude.
- The external Unreal volume must be mounted; if /Volumes/Unreal is missing, nothing here can be verified against code.
- For meaningful completed work, append a source-backed capsule with the vault's memory-capsule.py tool (project id red_hope). Canonical context notes are not completion logs.
