---
name: kyusu-level
description: Block out, inspect and fix Sencha levels through Kyusu's level operations (spaces, doors, walls, reachability, floor plans), and hand the result to a designer as a proposal file. Use when asked to lay out rooms, corridors or doors in a Sencha or Kyusu level, to check whether a level connects, or to read an existing level's layout.
---

# Kyusu level authoring

Change levels through operations, never by writing `.sscene` text. Operations
keep ids, generated walls, undo and diagnostics consistent; hand edits do not.

## Run

```
<sencha>/build/editor/kyusu_batch/kyusu_batch --project <project.senchaproj> \
    --level <level.sscene> --script <ops.txt>
```

`--world <x.sworld> [--zone <id or name>]` opens a world at a zone instead;
without `--script` lines come from stdin. One operation per line, one JSON
answer per line: `{operation, status: ok|refused|error, code, message, data}`.
A refusal says why in `code` and the script carries on; exit status 1 means a
line was malformed.

- A line is `name key=value ...` or `name {json5 object}`.
- Vectors are `[x, y, z]`; strings with spaces go in single quotes.
- A line starting with `#` is a comment, so name entities by their bare
  16-hex id or as `@Name`.
- The batch never saves the level. What it hands on is a proposal file.

## Frame

Metres, +Y up, north is -Z, east is +X. A space is a volume of air between
`min` and `max` (`min.y` is its floor); its walls are generated outward by the
wall thickness and are not entities of their own. Rooms exactly one wall
thickness apart share that wall. A door is placed by a point in or near a wall
(`opening.create at=[...]`), and the wall decides which room owns it; a sill
of 0 makes a doorway.

## Loop

1. Look: `conventions.get`, `level.summary`, `world.summary` for a world, and
   `plan.section height=1.2 image=<absolute path>.png`, then read the image.
2. `draft.begin label='...' rationale='...'`: everything after edits a copy,
   and queries read the copy.
3. Build: `space.create`, `opening.create`, `space.move` (carry stretches the
   corridor a door opens onto), `space.resize_face`, `opening.move`.
4. Check: `level.diagnostics` must come back empty or be explained;
   `nav.reachable from=[...] to=[...]` between rooms, with points just above a
   floor; `measure.clearance` in corridors; `deps subject=@Name`; the plan
   again.
5. `draft.save path=<project dir>/.proposals/<name>.sproposal author=claude`,
   then tell the designer the file, what it proposes and what to look at.

A refused step fails the draft. Fix the cause, `draft.resume`, and carry on;
the refused step is not kept. `draft.diff` shows the steps and what changed.

## The designer's side, in Kyusu's console

`draft.open path=<file>` replays the proposal into a draft, and its answer's
`moved_since` lists entities edited after the proposal was written.
`draft.accept` applies it as one undo step; `draft.discard` drops it.

## Pitfalls

- Space names are unique, so `@Name` always resolves.
- Shelled spaces that overlap lose their walls (`space.overlap`). Leave
  exactly one wall thickness (0.25 by default) between rooms that share a
  wall, or more to keep both walls.
- `nav.reachable` needs the project's navigation settings; `nav.unavailable`
  means it has none.
- In a world, operations read the focused zone; choose it with `--zone`.

## Reference

[reference.md](reference.md) lists every operation, its parameters and the
keys its answer carries. It is generated, never edited:
`kyusu_batch --reference > .claude/skills/kyusu-level/reference.md`.
