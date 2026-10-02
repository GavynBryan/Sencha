# Kyusu interaction grammar

How a person operates Kyusu, independent of any one workspace, and how the code
is split so that this grammar can be replaced without rewriting the workspaces.

**Status:** design of record for the Shoji migration. The shell described here
is being built in stages (plan: the Kyusu-on-Shoji epic). Today's Kyusu is still
the ImGui dock shell described in `editor/ARCHITECTURE.md`; where this document
and the code disagree about *current* behaviour, the code wins.

## 1. The principle

The content being authored dominates the window. Interface appears when the
person needs to act on something, and gets out of the way when they do not.

Kyusu is *aesthetically maximalist and interaction-minimalist*. Its look — the
Metalheart workstation: tactile machinery, readouts, lamps, physical-looking
controls — is spent on a small number of instruments that persist, not on
frames around content. There is no docking, no layout file and no UI whose
job is resizing other UI.

Surfaces that open over the content -- the selection card, drawer and sheet,
the explorer tray and outline -- have a default place that keeps clear of the
instruments they would cover (the tray narrows between the station and the
card, the outline ends above the station, a sheet moves past an outline),
but no smaller than it can still be used (a tray 420dp wide, an outline
200dp tall, a sheet 260dp tall); past that it keeps its size and overlaps.
Where two overlap, the one last opened, pressed or dragged is in front. Each
can also be dragged by its header, and a double click on its title puts it
back. `shell/SurfacePlacement` holds both
rules, and `ShellWindow` measures the instruments for it.

## 2. The grammar is a presentation policy

Everything in this document from §3 onward is **policy**: where things appear,
how they look, which gestures summon them. It is deliberately replaceable. The
code is split so that a different grammar — a radial-only interface, a
late-90s scientific workstation, even a paneled editor — means rewriting the
shell and its documents, not the workspaces.

What survives any grammar:

| Mechanism | Lives in |
| --- | --- |
| Documents, commands, undo journal | `common/src/documents`, `common/src/commands` |
| Action definitions and their handlers | `common/src/actions` |
| Property model and edit sessions | `common/src/properties` |
| Selection, tools, picking, manipulation | `common/src/selection`, `tools`, workspace libraries |
| Active resources | `common/src/resources` |
| Problems and notices | `common/src/problems`, `notices` |
| Workspace facets | `common/src/workspaces/facets` |
| Shoji surfaces, host images, rows | `engine/include/ui` |

What a grammar change rewrites: `common/src/shell/` (the window controller,
placement policy and presenters) and the documents in `editor/ui/shell`,
`editor/ui/kit` and `editor/ui/art`.

### Workspaces expose semantics, never positions

A workspace does not know that its active resource is drawn as a cartridge in
the lower-right corner. It says *my active tool consumes a material, and it is
this one*. It exposes **facets** — small read models plus action handlers:

| Facet | Says | Workspaces |
| --- | --- | --- |
| Documents | what is open, which is active, what is dirty, how to save | all |
| Operation | the current tool or mode and its variants | Level, Animation, UI Preview, Materials |
| Manipulation | transform mode, space, pivot, snap, grid | Level |
| Resources | resource roles the operation consumes, and their values | Level, Materials, Animation |
| Selection | what is selected, as property sources and relevant verbs | Level, UI Preview, Animation |
| Transport | cook / play / stop state | Level |
| Timeline | time, range, playing, marks | Animation |
| View | arrangement, projection, display modes | Level, Materials, Animation |

Documents that genuinely belong to one workspace (the World explorer's tree,
the rules drawer, the data sheet) are requested by **role** — `Content`,
`Explorer`, `ContextDetail`, `Bench` — and the shell's placement policy
decides where a role goes, how it is anchored, how it animates and how it is
dismissed. A workspace receives a narrow `WorkspaceWindow` (pointer capture,
relative mouse, dialog parent, content rect, role documents, action context)
and nothing that names chrome.

A layering rule keeps this honest: workspace libraries may not include the
shell's presenters or policy, and a headless fake shell drives the real
workspaces through facets alone.

### Shoji learns nothing about this grammar

Shoji is the runtime UI for games as well. It gains generic primitives only —
surfaces on any window, images the host supplies, richer rows — and never
concepts such as "drawer" or "explorer". The grammar is an RCSS kit plus editor
controllers; a game may author the same patterns from the same kit.

## 3. Anatomy of a window

```
┌─[≡][ LEVEL ◉ ][ glass.sscene ● ]──────── notices ────────[⚠2][COOK ▸ PLAY ■][_ □ ×]┐ rail
│                                                              [PERSP · LIT · 1▢] │ view
│                                                                                   │
│                          CONTENT — full bleed                        ┌ card ────┐ │ context
│                                                                      └──────────┘ │
│ [▣ CARVE · Quad ][ MOVE · WORLD · 16u ◌]             [▦ Concrete_01 ][▫][▫][▫]    │ stations
└───────────────────────────────────────────────────────────────────────────────────┘
```

The same place means the same thing in every workspace:

| Place | Meaning | Contents |
| --- | --- | --- |
| Rail, left | identity | hatch (≡), workspace dial, document dial with dirty lamp |
| Rail, right | consequences | problems lamp, transport where it applies, window buttons |
| Content, top-right | how I am seeing | view instrument |
| Bottom-left | what my hands are doing | operation station (the Q wheel's face) and manipulation readout (the Z wheel's face) |
| Bottom-right | what I am holding | the primary active resource, plus stash |
| Right edge | what I am looking at | selection card, expanding to the details drawer |
| Bottom edge | the work's axis, when it has one | e.g. Animation's timeline; empty otherwise |
| Top-centre | what just happened | transient notices |

The rail is also the window caption (client decorations): it is where the
window is dragged from. It is the only full-width chrome.

Every persistent instrument is **a readout that is also a button**. The
operation station shows the current tool and opens the tool wheel when clicked;
the resource cartridge shows the active material and opens its explorer. Nothing
persistent exists only to advertise operations.

## 4. Depth

Interface comes in increasing commitment to a task. The rungs measure *scope* —
a choice, a value, an object, a library, a domain — not size:

| Rung | Holds | Summoned by | Leaves by |
| --- | --- | --- | --- |
| Quasimode | a choice among tools or modes | holding Q / Z, or clicking a station | releasing (commits), Esc |
| Popover | one decision | clicking an instrument | choosing, clicking elsewhere, Esc |
| Card | the primary fields and verbs of the current object | selecting it, or a tool with parameters | deselecting |
| Drawer | everything about the object | expanding the card | collapsing, Esc; a pinned drawer stays |
| Explorer | a library: materials, prefabs, world structure, assets | a slot, a key, the palette | committing a pick, Esc |
| Workspace | a whole authoring domain | "Edit in Materials…" and similar | the rail's return path |

Rules:

- **Esc steps down exactly one rung.** It dismisses the most recent dismissible
  transient; with none open, the workspace's own escape policy runs (cancel an
  interaction, then deselect).
- **Cards are a projection of selection**, not windows. They exist while the
  selection does.
- **Explorers keep the world visible** and preview live against it: hovering a
  material previews it on the selected faces, as a pending edit that leaving
  cancels.
- **Escalating to another workspace leaves a way back.** The rail shows a
  return path ("◂ Level"), and the originating workspace keeps its state.
- **Pinning is the only persistent-surface concession.** A pinned drawer is a
  per-workspace setting, not a layout.

In code the ladder is not a type hierarchy. Transient surfaces are generic
entries — owner, parent, dismiss policy, whether Esc reaches it, priority,
persistence — and "popover", "drawer" and "explorer" are presets of those
fields. A nested explorer, two cards or a comparison surface needs no new
mechanism.

## 5. Finding things without knowing the shortcuts

Expert operation should be one chord away; nothing should *require* a chord.

- Every instrument shows its chord on hover. Chords are read from the action
  catalog, never written into a document, so a rebinding shows up everywhere.
- Clicking a station opens the same wheel its key opens, in click mode, so a
  pointer user learns the wheel's geometry and graduates to the key.
- The hatch lists every action by category, with chords: a menu with no
  menu bar.
- The command palette (Ctrl+K) searches actions, workspaces and explorers.
- A one-line hint ("hold Q for tools") shows until the wheel has been used.

## 6. Actions

An action is a *definition* (id, label, category, icon, default chord) and, at
any moment, possibly a *handler* (enabled, checked, invoke) supplied by
whichever context currently claims it. Each window resolves an action through
a stack of contexts, innermost first:

1. the focused shell surface (an explorer claims `edit.delete` for assets),
2. the active tool or element mode,
3. the active document,
4. the active workspace,
5. the window,
6. the application.

A focused text field sits ahead of the stack, so Delete in a field edits text.
The catalog never holds callbacks: handlers are registered by their context and
disappear with it. Every definition is also a console command of the same id,
so scripts, startup `+commands`, keymaps and the palette reach one table.

## 7. Properties

Authoring state is edited through one lifecycle — begin, preview, commit,
cancel — keyed by a stable property path, against whichever document owns it
(component bytes, a data document, a tool's parameters). Every interruption
ends the session: Esc, focus loss, a tool switch, hiding the workspace, closing
the document.

The neutral model is a tree of *properties* (identity, descriptor, value,
children, capabilities, a presentation hint). It has no notion of a row. The
card and the drawer are two presentations of the same tree at different
relevance; a purpose-built editor (a modifier stack, a blendspace, a colour
instrument) is a third, and still commits only through the same sessions or
commands. Which fields a compact card shows is editor data — a card profile per
component — not schema.

## 8. Motion

Transitions animate transform and opacity only, never layout, so they cost no
relayout and never move what the person is aiming at. They last 90–160 ms, are
interruptible, and never gate input: acting mid-transition acts. They express
mechanism — a card slides out of its station, a dial label rolls, a cartridge
seats — not decoration.

`editor.ui.motion 0` removes motion. It is an accessibility setting, and it is
also how golden images stay deterministic (the UI clock is host-driven).

## 9. What persists

Only settings a person would recognise as settings: theme, UI scale, keybinds,
motion, per-workspace view arrangement, pinned drawer, explorer view options,
hints seen, and recent documents per project. They are archived cvars through
the existing settings archive.

No position, size or layout is persisted. Where a surface was dragged lasts
for the session, per window.

## 10. Metalheart

The look is built from two materials, both themeable:

- **RCSS-composed machinery** — plates, bezels, seams, keys that depress, lamps
  with off/idle/active/warn/fault states, segmented readouts, dials, cartridges,
  trays — in a shared kit (`editor/ui/kit/`).
- **A small hand-drawn SVG set** (`editor/ui/art/`) for what boxes and borders
  cannot make: screw heads, vents, dial faces, cartridge shells, bezel corners.
  White artwork, cooked to textures, tinted per theme role with `image-color`.
  No artwork is generated by code.

The theme sets colours and nothing else (`docs/ui/architecture.md` §14), so a
theme change restyles without relayout. Geometry belongs to the documents.

Gradients, shadows and filters are outside Shoji's current render contract;
glow and depth come from art and layered boxes. "Metalheart" names the
aesthetic in documents and themes; it is never an identifier.

## 11. Workspaces on the grammar

| Workspace | Content | Operation | Holding | Looking at | Explorers | Axis |
| --- | --- | --- | --- | --- | --- | --- |
| Level Design | the world | tools (Q), manipulation (Z) | material / prefab | object, brush, elements | materials, prefabs, world | — |
| Materials | the lit preview stage | preview primitive | texture for the focused slot | parameters (drawer, pinned by default) | textures, material files | — |
| Data | the asset, typeset as a sheet | subtype lens | — | field help beside the focused field | data assets | — |
| UI Preview | the previewed surface | interact / inspect | model preset | picked element | documents, vocabulary | — |
| Animation | the rig on a stage | lenses (selection, layers, blends, events, root motion, requests, facts) | rig and scenario | bone, event mark, rule | content, rigs and scenarios; benches for lab, batch, migration | timeline |
| Project | the project rack | — | — | project | create project | — |

Level Design's flows are specified in `docs/editor/level-design.md`.
Animation's layout is a proposal pending its own design review.
