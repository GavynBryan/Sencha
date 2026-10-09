# Kyusu interaction grammar

How a person operates Kyusu, independent of any one workspace, and how the code
is split so that this grammar can be replaced without rewriting the workspaces.

**Status:** describes the shell as built. Where this document and the code
disagree about current behaviour, the code wins and this document is fixed.

## 1. The principle

Two priorities decide every choice:

- **Least travel.** The left hand stays on WASD and the mouse stays on the
  work. What is done often is a held key under the left hand or the thumb, or
  a gesture on the object, never a trip to an edge of the window.
- **Total clarity.** The person always knows where they are, what is selected
  and what a change will do, and text reads against what is behind it.

Interface appears when the person asks for it and goes away when they are done
with it. It activates when wanted and never when not, and it does not make an
accident easy. Kyusu is *aesthetically maximalist and interaction-minimalist*:
its look — the Metalheart workstation of plates, lamps and readouts — is spent
on a few instruments that persist, not on frames around content.

Surfaces never resize themselves. A summoned explorer opens where it usually
lives if that space is free of the instruments and the selection, otherwise in
the nearest free space, and with no room at all it opens at home over whatever
is there; then it stays where it opened. The selection's sheet keeps to its own
place. Either moves by its header and sizes by its corner, and keeps what the
person chose for the session; a double click on its title puts it back. The one
pressed last is in front, and Escape closes the front one (`shell/SurfacePlacement`).

## 2. The grammar is a presentation policy

Everything from §3 onward is **policy**: where things appear, how they look,
which gestures summon them. It is replaceable: a different grammar means
rewriting the shell and its documents, not the workspaces.

What survives any grammar:

| Mechanism | Lives in |
| --- | --- |
| Documents, commands, undo journal | `common/src/documents`, `common/src/commands` |
| Action definitions and their handlers | `common/src/actions` |
| Property model and edit sessions | `common/src/properties` |
| Selection, tools, picking, manipulation | `common/src/selection`, `tools`, workspace libraries |
| Active resources | `common/src/resources` |
| Problems and notices | `common/src/problems`, `notices` |
| Workspace facets | `common/src/workspaces/WorkspaceFacets.h` |
| Shoji surfaces, host images, rows | `engine/include/ui` |

What a grammar change rewrites: `common/src/shell/` (the window controller,
placement and presenters) and the documents in `editor/ui/`.

### Workspaces expose semantics, never positions

A workspace says what it has — *my tool consumes a material, and it is this
one*; *this is selected, these are its properties and actions*; *the work is in
this world, this zone* — through **facets**: small read models plus action
handlers.

| Facet | Says |
| --- | --- |
| Documents | what is open, which is active, what has changed |
| Operation | the current tool or mode and its variants |
| Manipulation | transform mode, space, pivot, snap, grid |
| Resources | what the operation consumes, and what is to hand |
| Selection | what is selected, as property sources and actions; shown always or only on request |
| Location | where the work is (world, graph, zone, selection), the hierarchy that reveals it, and the map |
| Explorers | what can be browsed: a hierarchy a page at a time, or files the Shelf chooses among |
| Transport | cook / play / stop state |
| Timeline | time, range, playing, marks, lanes |
| View | arrangement, projection, display modes |

A workspace receives a narrow `WorkspaceWindow` — the platform window, its
surfaces by role, the window's action stack — and nothing that names chrome. A
layering rule keeps this honest: workspace libraries may not include the
shell.

### Shoji learns nothing about this grammar

Shoji is the runtime UI for games as well. It gains generic primitives only —
surfaces on any window, images the host supplies, richer rows, antialiased
sectors — and never concepts such as "tab" or "explorer".

## 3. Anatomy of a window

```
┌─[≡][LEVEL traversal3 ●][MATERIALS brick][+]────── notices ──────[⚠2][COOK ▸ PLAY ■][_ □ ×]┐ rail
│ [MAP  traversal3 ▸ Graph 1 ▸ Room 3 ▸ Brush   Tab hierarchy]          [PERSP · LIT · 1▢]  │ where · view
│                                                                                            │
│                               CONTENT — full bleed                                         │
│                                                                                            │
│ [▣ CARVE · Quad ][ MOVE · WORLD · 16u ◌][▦ concrete ][▫][▫][▫]                              │ hands
└────────────────────────────────────────────────────────────────────────────────────────────┘
```

| Place | Meaning | Contents |
| --- | --- | --- |
| Rail, left | what is open | hatch (≡); a tab per open asset, its editor named small beside it, a lamp when changed; + to raise the Shelf |
| Rail, right | consequences | problems lamp, transport where it applies, window buttons |
| Under the rail, left | where the work is | breadcrumb: MAP, then world ▸ graph ▸ zone (the one the camera is in) ▸ selection; a place opens the hierarchy at it, the selection opens its sheet |
| Content, top-right | how I am seeing | view instrument |
| Bottom-left | what my hands are doing and holding | operation station (the Q wheel's face), manipulation readout (the Z wheel's face), the resource cartridge and stash |
| Bottom edge | the work's axis, when it has one; the project's content | Animation's tape deck; the Shelf, which the instruments and the deck ride on |
| Top-centre | what just happened | transient notices |

A tab is an open asset; its type picks the editor. Choosing a tab shows its
editor and the asset; dragging a tab down out of the rail gives its editor a
window of its own (`shell/AssetTabs`). There are no workspaces to switch
between apart from the assets they hold.

**The Shelf** is the project's content along the bottom edge, and it belongs to
the window, not to a tab: what it was showing is still there when it comes
back. Typing finds anything — a name, `t:material`, `in:dev` — ranked by how
well the name holds it; kinds, folders and lists (recent, open, changed)
narrow it; the chosen file's facts sit beside the tiles; Enter or OPEN opens it
in the editor that opens its kind (`WorkspaceKind::Opens`), and an editor with
nothing open raises the Shelf narrowed to what it opens. Raised, it lies over
the work and goes when something opens; pinned, it stays and the work ends
above it; its top edge sizes it (`shell/Shelf`, `project/AssetQuery`). Files
are pictured through one registry every workspace shares
(`thumbnails/AssetPictures`): a material by its base color texture tinted by
its base color, a texture by itself, a scene by the level editor's studio while
the level editor is open; anything else by its kind's icon. Ctrl and the wheel
over the tiles size them.

The Shelf is also where a file is chosen *for* something (`ShelfPick`): the
material in hand (M), the prefab to place (P), an asset field on the sheet.
Raised to choose, it says what it is choosing for, offers only the kinds the
chooser accepts (or exactly the files a field allows), never the level being
edited as a prefab of itself, and lights the one in use. A click picks — the
material goes in hand at once — and Enter, a double click or CHOOSE commits
and lowers it; NONE clears a field. A workspace declares such an explorer as
`ExplorerShape::Files` with the kinds it accepts and is handed the choice; it
never lists files itself.

The Shelf knows what names what (`project/ReferenceIndex`, one per project,
read from the documents themselves — every `asset://` path in a material,
scene or data asset, a part of a file as the file, a cooked scene as its
source). The chosen file's preview lists what it uses and what uses it; a
click follows to that file and the back key in the bar returns. What a file
names that is nowhere — in the project or in the engine's content mounted
beside it — is listed under it, and such files are the **Problems** list;
**Unused** lists what nothing names (a level is opened rather than named, so
levels sit there until a game's settings name them). Ctrl+click on an asset
field shows its file on the Shelf. The index is read again as the Shelf
rises, only documents whose size or time changed.

The chosen file is renamed (F2, asked as where it is in the project, so a
slash moves it to another folder), duplicated beside itself (Ctrl+D) and
trashed (Delete) through the edit actions, each one step that Ctrl+Z takes
back (`project/ProjectFileSteps`, pipeline Decision A). A trash asks first
only when other files use it, and lists them. The Shelf claims these actions
while raised; its search field keeps the keys it edits with and passes every
other chord on, so F2, Ctrl+D, Ctrl+Z, and Delete in an empty field all
reach them while typing.

The rail is also the window caption (client decorations) and the only
full-width chrome. Every persistent instrument is **a readout that is also a
button**: the station shows the tool and opens its wheel, the cartridge shows
what is held and opens its explorer.

## 4. Getting to things

Interface comes in increasing commitment to a task:

| Rung | Holds | Summoned by | Leaves by |
| --- | --- | --- | --- |
| Wheel | a choice: a tool (Q), a transform (Z), what to do to *this* (Space) | holding the key, or clicking a station | releasing (commits; in the middle, nothing), Esc |
| Handle | a value with a place on the object | being on the selected object | letting go (commits), Esc |
| Popover | one decision | clicking an instrument | choosing, clicking elsewhere, Esc |
| Sheet | everything about the selection | the selection's part of the breadcrumb, More… on the Space wheel, or the palette | Esc, or its part again |
| Explorer | the hierarchy; a library is the Shelf choosing | a slot, a key (Tab for the hierarchy), the breadcrumb, the palette | committing a pick, Esc |
| Shelf | the project's content | Shift+Space, + on the rail, an editor with nothing open | opening something, Esc; pinned, it stays |
| Map | the world from above | MAP in the breadcrumb | clicking a zone (goes there), Esc |

**The Space wheel** is about what is under the pointer, or the selection when
nothing is (`actions/ContextMenuModel`). Its hub reads out what the thing is and
its values, so holding Space and letting go in the middle only looks. Its
petals are the thing's choices — each a fan of options with the one in effect
marked; an asset's fan is what is to hand, pictured, then Browse… — its
actions that can be done now, and More… for the full sheet. Disabled actions
are never offered.

**Handles** put a selected object's spatial values on it: a size is a ring
facing each view with a knob to drag, small at rest and larger with the pointer
near; an angle — a spot light's cone, a camera's field of view — is the pair of
edges it opens about the object's forward axis, with a knob on one edge; a
number with no place of its own changes by scrolling over the object.
The value shows beside the handle only while it changes (`PropertyHandles`).

A workspace whose properties *are* its content (a data asset, a material, an
animation lens) shows its card whenever something is selected. The Level's
selection is shown only on request, so the view stays clear.

**Esc steps down one rung**: an open popover, then the surface in front, then
the workspace's own escape policy (cancel an interaction, then deselect).

In code the rungs are not a type hierarchy. Transient surfaces are generic
entries — owner, dismiss policy, whether Esc reaches it — and "popover" and
"explorer" are presets of those fields.

## 5. Finding things without knowing the shortcuts

Expert operation is one chord away; nothing *requires* a chord.

- The breadcrumb always says where the camera and the selection are; each
  place in it opens the hierarchy showing it, and the selection opens its
  sheet. Its hint names the hierarchy's chord.
- Every instrument shows its chord on hover. Chords are read from the action
  catalog, never written into a document, so a rebinding shows up everywhere.
- Clicking a station opens the same wheel its key opens, in click mode.
- The hatch lists every action by category, with chords.
- The command palette (Ctrl+K) searches actions, workspaces and explorers.

## 6. Actions

An action is a *definition* (id, label, category, icon, default chord) and, at
any moment, possibly a *handler* (enabled, checked, invoke) supplied by
whichever context claims it. Each window resolves an action through a stack of
contexts, innermost first: the focused shell surface, the active tool or
element mode, the active workspace, the window, the application. A focused text
field sits ahead of the stack, so Delete in a field edits text; the shell's
controls take no part in Tab traversal, so Tab is always the editor's. The
catalog never holds callbacks, and every definition is also a console command
of the same id.

## 7. Properties

Authoring state is edited through one lifecycle — begin, preview, commit,
cancel — keyed by a stable property path, against whichever document owns it.
Every interruption ends the session: Esc, focus loss, a tool switch, hiding the
workspace, the selection moving on. A wheel fan, a handle and the full sheet are
three presentations of the same properties, and each commits only through the
same sessions or commands.

**The sheet.** Its title is the selection's name, typed over to rename, with
how else it is known beneath (its id; the prefab it is an instance of). Adding
a component comes first, each one offered saying what it would also add.
Components follow, each heading carrying what can be done about it: its own
actions (a brush's bake and export), its verbs, and a cross that removes it —
quiet until the pointer is on it, and undone by the chord the notice names.
Groups fold. Last, closed until opened, are the components the engine put on
the entity because of the authored ones, under what each came from. Nothing
piles up at the bottom: what can be done to the selection as a whole is on the
Space wheel and in the palette.

**Numbers.** A number is a box, filled behind its value as far as it is
between its bounds; a vector is its numbers side by side, each named in the
gizmo's colours; a rotation is its pitch, yaw and roll in degrees. Dragged
across, a box changes with the pointer held still and hidden — Shift for fine
steps, Ctrl for coarse ones — and the whole drag is one step that Esc puts
back. Let go where it was pressed, it types instead: Enter or Tab keeps what
was typed, Tab moving on to the next number, and Esc drops it.

**Several at once.** Several selected objects are one sheet: every component
any of them holds, one only some hold saying how many and offering itself to
the rest, and a field they disagree on reading "— differs". Typing sets every
one alike; dragging moves each from its own value. Whatever an edit touches is
one step. A component that draws its own rows — a brush's modifier stack — is
edited one object at a time.

## 8. Motion

Transitions animate transform and opacity only, never layout, are interruptible
and never gate input. A surface being moved is laid out, not transformed, so it
stays on whole pixels and is hit where it is drawn. `editor.ui.motion 0` removes
motion.

## 9. What persists

Only settings a person would recognise as settings: theme, UI scale, keybinds,
motion, per-workspace view arrangement, and recent documents per project. They
are archived cvars. Where a surface was moved or how it was sized lasts for the
session, per window.

## 10. Metalheart and contrast

The look is RCSS-composed machinery in shared documents plus a small hand-drawn
SVG set, tinted per theme role; no artwork is generated by code. The theme sets
colours and nothing else, so a theme change restyles without relayout.

Text must read: primary and secondary text reach at least 4.5:1 against every
surface they sit on, in every bundled theme, which a test enforces. Gradients,
shadows and filters are outside Shoji's render contract; "Metalheart" names the
aesthetic, never an identifier.

## 11. Workspaces on the grammar

| Workspace | Content | Operation | Holding | Selection | Explorers | Axis |
| --- | --- | --- | --- | --- | --- | --- |
| Level Design | the world, or the map | tools (Q), manipulation (Z) | material / prefab | on request: Space wheel, handles, sheet | hierarchy (Tab); materials and prefabs on the Shelf | — |
| Materials | the lit preview stage | preview primitive | — | parameters, shown | the Shelf | — |
| Data | the asset, typeset as a sheet | subtype lens | — | the sheet | the Shelf | — |
| UI Preview | the previewed surface | interact / inspect | — | picked element, shown | documents, elements, vocabulary | — |
| Animation | the rig on a stage | lenses | rig and scenario | lens cards, shown | content, rigs, skeleton | tape deck |
| Project | the project rack | — | — | project | — | — |

Level Design's flows are in `docs/editor/level-design.md`; Animation's in
`docs/editor/animation-workspace.md`.

### Not built yet

- A tab's own close key; closing goes through Ctrl+W or the hatch.
- Hovering a material on the Shelf previewing it on the selected faces.
