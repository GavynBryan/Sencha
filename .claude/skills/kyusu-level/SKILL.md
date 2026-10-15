---
name: kyusu-level
description: Block out, inspect and fix Sencha levels through Kyusu's level operations (spaces, doors, walls, brushes, materials, prefabs, reachability, floor plans), live beside a designer in a running Kyusu or headless with a proposal file. Use when asked to lay out rooms, corridors or doors in a Sencha or Kyusu level, to act on "this", "here" or "where I'm looking" in Kyusu, to check whether a level connects, or to read an existing level's layout.
---

# Kyusu level authoring

Change levels through operations, never by writing `.sscene` text. Operations
keep ids, generated walls, undo and diagnostics consistent; hand edits do not.

## Two ways in

**Live, beside the designer.** Kyusu listens while `editor.operations.socket`
is on (console: `set editor.operations.socket 1`; it stays on).

```
<sencha>/build/editor/kyusu_client/kyusu_client <operation> [key=value ...]
<sencha>/build/editor/kyusu_client/kyusu_client < ops.txt
<sencha>/build/editor/kyusu_client/kyusu_client --events 5
```

One answer per line. `nothing is listening` means Kyusu isn't running or the
socket is off: ask the designer. Operations act on the level open in Kyusu, and
edits made outside a draft land in it directly as undo steps, so draft anything
the designer has not asked to see happen immediately.

**Headless, no Kyusu.**

```
<sencha>/build/editor/kyusu_batch/kyusu_batch --project <project.senchaproj> \
    --level <level.sscene> --script <ops.txt>
```

`--world <x.sworld> [--zone <id or name>]` opens a world at a zone instead;
without `--script` lines come from stdin. The batch never saves the level; it
hands work on as a proposal file. `view.*` needs a running Kyusu.

## Lines and answers

- A line is `name key=value ...` or `name {json5 object}`.
- Vectors are `[x, y, z]`; strings with spaces go in single quotes; lists of
  subjects are `subjects=['@Hall', '@Shed']`.
- A line starting with `#` is a comment, so name entities by their bare
  16-hex id or as `@Name`.
- Every answer is `{operation, status: ok|refused|error, code, message, data}`.
  A refusal's `code` says why (`subject.ambiguous` lists `candidates` in
  `detail`); the script carries on. A mutation's `data` lists what it
  `created`, `modified` and `deleted`, and counts the wall brushes spaces
  rebuilt for it in `walls`.

## Frame

Metres, +Y up, north is -Z, east is +X. A space is a volume of air between
`min` and `max` (`min.y` is its floor). Its walls stand outward by the wall
thickness as wall brushes under the space (kind `wall`): each wall's room
side ("Hall north wall") and its outside ("Hall north wall outside") apart.
The space rebuilds them whenever it or a door in them changes, in the same
undo step; `level.summary` leaves them out and `deps` counts them. Rooms
exactly one wall thickness apart share that wall. A door is placed by a point in or near a wall
(`opening.create at=[...]`), and the wall decides which room owns it; a sill
of 0 makes a doorway. Faces are named by the direction they look (`north`,
`up`, ...) or by a point on them, never by index.

## "This", "here", "where I'm looking"

`view.context` answers them from the running editor:
- `aim`: the first solid surface at the middle of the view: `point`, `normal`,
  the `entity` it belongs to (a wall brush answers as itself), and the
  space it is `facing_into`. "Here" on a wall is `aim.point`: a door there is
  `opening.create at=<aim.point>`.
- `selection`: what "this" means, with face indices when faces are selected.
- `camera`: the eye, `facing` as a compass bearing, `pitch`; `in_space`, and
  `in_view`: the spaces the view sees, nearest first.

`view.select subjects=[...] frame=true` hands a selection back;
`view.frame subjects=[...]` turns the view onto something without selecting it.

## Levels built without spaces

`space.propose` (in a draft) reads rooms and passages off the walkable
surface and proposes unshelled spaces for them. It is a heuristic: look at
`plan.section` before accepting, and rename what it called Room and Passage.

## Loop

1. Look: `conventions.get`, `level.summary`, `world.summary` for a world,
   `view.context` when live, and `plan.section height=1.2 image=<absolute
   path>.png`, then read the image. Live, also `changes.mark`.
2. `draft.begin label='...' rationale='...'`: everything after edits a copy,
   and queries read the copy. Live, the designer sees the draft drawn over the
   level in green.
3. Build: `space.create`, `opening.create`, `space.move` (carry stretches the
   corridor a door opens onto), `space.resize_face`, `opening.move`;
   `brush.box`, `brush.extrude`, `brush.clip`, `brush.carve` for solid
   geometry; `entity.duplicate`, `entity.delete`, `entity.reparent`,
   `entity.rename`; `assets.list query='t:material stone'` then
   `material.apply`; `assets.list query='t:scene'` then `instance.place`;
   `component.set` for a field `entity.describe` shows (not
   `generated_wall`, which the space keeps itself).
4. Check: `level.diagnostics` must come back empty or be explained;
   `nav.reachable from=[...] to=[...]` between rooms, with points just above a
   floor; `space.graph` for every room at once (pass `held=[...]` and
   `denied=[...]` for a scenario); `measure.clearance` in corridors;
   `deps subject=@Name`; the plan again. In a world, `world.crossings` lists
   floor that runs into the next zone with no dock; `world.focus target=world`
   and `dock.place_at` (in a draft) propose one. `world.reachable` and
   `world.bypasses` ask across zones: a zone is left only through a dock (a
   dock in a floor is a drop), and gates stay shut unless `open_gates` says.
5. Hand it over. Live: tell the designer what the draft proposes; they accept
   in Kyusu (Ctrl+Enter in the Proposals tray) or ask you to `draft.accept`.
   Before accepting, `changes.since mark=<mark>` says what they changed
   meanwhile; an accept that touches the same entities refuses with
   `draft.conflict`, and `draft.rebase` replays the draft over their edits.
   Headless: `draft.save path=<project dir>/.proposals/<name>.sproposal
   author=claude`, then tell the designer the file, what it proposes and what
   to look at.

Leave what the designer should look at, or what you could not decide, as a
note: `note.add text='...' subject=@Name` (or `at=[...]`); `notes.list` shows
the open ones, including theirs to you.

A refused step fails the draft. Fix the cause, `draft.resume`, and carry on;
the refused step is not kept. `draft.diff` shows the steps and what changed.
A proposal opened with `draft.open` that stops partway stays failed, its
`failure` naming the step, operation and code: it cannot be accepted or
resumed, and `draft.rebase` replays the rest once the level allows it.

## The designer's side, in Kyusu

The Proposals tray (Shift+D) lists open drafts and the files in
`<project>/.proposals`; picking one shows it, drawn over the level in green
(deleted entities in red). Ctrl+Enter accepts the shown proposal as one undo
step and Ctrl+Backspace rejects it. From the console the same is `draft.open
path=<file>` (its `moved_since` lists entities edited after the proposal was
written), `draft.accept` and `draft.discard`. `kyusu_client --events` shows
what they do as they do it: `selection`, `saved`, `draft.accepted`,
`draft.discarded`.

## Pitfalls

- Space names are unique, so `@Name` always resolves: no entity may take a
  space's name, nor a space another entity's (`space.name_taken`). A copy
  takes the next free name ("Hall 1").
- Shelled spaces that overlap lose their walls (`space.overlap`). Leave
  exactly one wall thickness (0.25 by default) between rooms that share a
  wall, or more to keep both walls.
- `brush.*` reshapes solid brushes only; a space's faces move with
  `space.resize_face`, and a door in a wall is `opening.create`, not
  `brush.carve`. Reshaping a wall brush by hand keeps it: the space stops
  rebuilding that wall (`wall.kept_stale` once the space moves on) until
  `shell.rebuild` takes it back. Painting it does not.
- An entity means its branch: deleting or copying a space takes its doors.
- `nav.reachable` needs the project's navigation settings; `nav.unavailable`
  means it has none.
- In a world, operations read the focused zone; choose it with `--zone`.

## Reference

[reference.md](reference.md) lists every operation, its parameters and the
keys its answer carries. It is generated, never edited:
`kyusu_batch --reference > .claude/skills/kyusu-level/reference.md`.
