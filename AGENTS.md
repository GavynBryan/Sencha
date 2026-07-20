# Repository Agent Instructions

`CLAUDE.md` is the authoritative repository-level engineering constitution for every coding agent working in Sencha, not only Claude.

Before planning, reviewing, or editing:

1. Read the root `CLAUDE.md` completely.
2. Follow its instruction precedence, repository-inspection workflow, architectural constraints, verification requirements, and definition of done.
3. Read any more specific `AGENTS.md` or `CLAUDE.md` in the subtree being changed. A subtree file may add local constraints, but it may not silently weaken the root constitution.
4. Treat plans and capitalized prose names as design intent, not proof that code or an extension point exists. Verify against the current tree.

Do not duplicate the root rules in this file. Duplication creates contradictory agent guidance.

When this file and the root `CLAUDE.md` appear to conflict, `CLAUDE.md` controls unless the user explicitly overrides the relevant constraint after the conflict is named.
