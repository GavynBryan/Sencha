# Level Design workspace: interaction design

The Level Design workspace on the Kyusu interaction grammar
(`docs/editor/interaction-grammar.md`). The world is the application surface;
a small set of instruments sits around it.

**Status:** design of record for the Shoji migration; the current ImGui
workspace is described in `editor/ARCHITECTURE.md`.

## At rest

The world fills the window below the rail. Persistent chrome is the rail, the
two bottom stations, the view instrument, and the resource cartridge — which
shows only while the active tool consumes a resource. Target: under 8% of a
1920×1080 window at scale 1, measured from the documents' laid-out boxes.

## Instruments

### Operation station (bottom-left)

Shows the active tool's icon, name and variant: `CARVE · Quad`. Clicking opens
the tool wheel in click mode (click a sector to commit); holding Q opens it in
hold mode (release to commit), unchanged from today. Hover shows the tool's
chord.

A tool with parameters raises a **tool card** from the station:

| Tool | Card |
| --- | --- |
| Brush | primitive, sides (cylinder), subdivisions (plane), outer/inner, Apply / Cancel |
| Face carve | mode, shape, turn, rise and segments (arch), pierce, Apply / Cancel |
| Clip | keep front / back / split, cap, Apply / Cancel |
| Edge cut | single / loop |
| Place | align to surface, rotation step |

The world-pinned Apply/Cancel buttons, the carve dial and the rise/segment
handles stay in the world; the card mirrors them for typed entry.

### Manipulation readout (beside the station)

`MOVE · WORLD · 16u` with a snap lamp and pivot glyph. Each segment opens a
popover: mode (as the Z wheel), space (World / Local / Grid), pivot and grid
origin, grid size and snap. The Z wheel gains a variant fan per mode for
space, so mode and space are one gesture. Existing chords stay:
Shift+Q/W/E/R, Ctrl+G, `[` / `]`, Shift+G, Alt+G, Ctrl+Shift+G.

### Resource cartridge (bottom-left, beside the station)

The resource the active tool consumes — the active material for brush,
texturing and select-with-faces; the active prefab for Place — as a cartridge
with thumbnail and name, plus three stash cartridges. Clicking the cartridge
opens its explorer; clicking a stash cartridge swaps it in. Shift+RMB samples a
face's material into it; Shift+T applies it to selected faces. The same
materials are the Space wheel's material fan on a face.

### View instrument (content, top-right)

`PERSP · LIT · 1▢`: projection, display mode and arrangement.

- One full-bleed view by default.
- Numpad 7 / 1 / 3 swap the view to top / front / side and numpad 5 back to
  perspective. Each of the four views keeps its own camera; swapping is
  choosing which one shows, so nothing is re-framed.
- Arrangements are one choice (Alt+1 / Alt+2 / Alt+4, kept in
  `editor.view.arrangement`): single, two-way split, quad. Two views are
  perspective beside the active ortho view; four are all of them. A split has
  one divider; nothing docks.
- Also here: lit / baked-lighting preview, debug view modes, zone bounds,
  streaming preview.

### Rail

Cook, Play and Stop sit in the rail's transport with a state lamp (idle,
cooking, playing, failed). Play saves every document, cooks if needed and
launches; a save conflict is a question to settle, and a failed save a
problem, instead of launching.
The transport popover holds the cook profile, lighting bake and hosted play.

## Selection and context

Lights, cameras and plain entities are pickable in the world through icon pick
proxies, so every selectable thing can be selected where it is.

Nothing about the selection sits on the view by default. **Hold Space** over
it — or over anything, which selects it first — for its wheel: the hub reads out
what it is and its values; the petals are its choices (a face's material, a
light's shadow settings), its actions that can be done now, and More… for the
full sheet.

| Selection | The Space wheel offers |
| --- | --- |
| Entity | its components' choices and toggles; duplicate, delete, hide, lock, move to a zone, and the rest |
| Faces | material (what is to hand, then Browse…), texture alignment; inset, extrude, flip, separate, delete, fit and center texture |
| Edges | bevel, bridge, soften / harden, dissolve |
| Vertices | weld, snap to grid |

**Values with a place in the world are handles on the object.** A selected
light's range is a ring with a knob to drag; its intensity changes by
scrolling over it. The value shows beside the handle while it changes.

**Parametric verbs** (inset, bevel) stage a preview that applies or cancels;
while one is staged the selection's sheet shows to apply it.

The **full sheet** (More…, or "All Properties" in the palette) shows every
component and field, adding and removing components, and instance overrides
with reset.

Every edit from the wheel, a handle or the sheet runs begin / preview / commit / cancel inside
the document's transaction, so undo, Esc and interruptions behave like a
viewport drag.

## Explorers

**Materials** (M, or the cartridge): a tray across the bottom of the world —
thumbnail grid, search, filters. Hovering a tile previews it on the selected
faces; leaving cancels the preview. Clicking loads the cartridge. With faces
selected, Enter or double-click applies to them.

**Prefabs** (P for the Place tool, or the cartridge): a tray of scene
thumbnails. Picking one loads the cartridge and arms Place: a ghost follows the
surface under the cursor, snapped to the grid; click stamps an instance, Esc
leaves. Pressing a tile and dragging into the world is the same gesture — the
tray tucks away and the world takes the pointer.

**Hierarchy** (Tab, or a part of the breadcrumb): a sheet on the left edge,
opened where there is room. The world, its graphs and zones, and the edited
zone's entities as one tree,
with visibility and lock, rename, reparent by dragging within the tree, move to
zone, zone manifest edits, streaming preview policy, and a map view of the zone
graph. Selecting a row selects the entity; F frames it.

## Workflows

| Task | Pointer | Keys |
| --- | --- | --- |
| Switch tool | click station → wheel | Q (hold, release), V B C X K P |
| Element mode | tool variant fan | 1–4, Shift+V |
| Transform mode and space | readout popovers | Z (+ space fan), Shift+Q/W/E/R, Ctrl+G |
| Grid | readout popover | `[` `]`, Shift+G, Alt+G |
| Choose material | cartridge → tray | M, Shift+RMB sample |
| Apply material | tray (Enter / double-click) | Shift+T |
| Place prefab | cartridge → tray → click world | P |
| A choice or action on it | — | Space (hold over it, release on a petal) |
| A value in the world | drag its handle, scroll over it | — |
| Every property | Space → More… | palette: "All Properties" |
| Hierarchy | breadcrumb part | Tab |
| World map, travel | MAP in the breadcrumb → click a zone | — |
| Frame selection | hierarchy row | F |
| Play | rail transport | palette: "Play" |
| Switch asset | its tab | — |
| Anything | hatch | Ctrl+K palette |

New chords are defaults in the action catalog and rebindable through
`keybinds.json`; the existing keymap keeps every row it has today.
