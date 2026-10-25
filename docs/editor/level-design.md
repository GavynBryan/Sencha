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
| Entity | yaw, turn step (align to surface planned) |

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

Every entity without a shape to click stands in the views as a marker at its
origin, so every selectable thing can be selected where it is. A component's
marker is the icon it is placed by (`SENCHA_PLACE_ICON`): a light, a camera, the
template's Player Start. An entity with no brush body, no such component and
nothing an adapter draws (an empty, a prefab's root, a placed mesh) wears the
empty mark. Who wears what is worked out again only when the scene's structure
changes (`EntityMarkers`).

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

**Delete** removes the whole selection as one step: the objects selected, and
the faces (or, with no faces, the edges, which dissolve) of brushes that are not
themselves going. Whatever cannot go says why in a notice — edges whose faces
cannot merge, vertices (weld them, or delete the faces they bound) — rather than
the key seeming to do nothing. After Face Carve or Edge Cut the Select tool is
left in Edge mode with the new edges selected, so a click picks edges until
Object mode (1) is chosen again.

**Values with a place in the world are handles on the object.** A selected
light's range is a ring with a knob to drag; a spot light's inner and outer
cone, and a camera's field of view, are edges with a knob at the end of one;
a light's intensity changes by scrolling over it. The value shows beside the
handle while it changes.

**Parametric verbs** (inset, bevel) stage a preview that applies or cancels;
while one is staged the selection's sheet shows to apply it.

The **full sheet** (the selection's part of the breadcrumb, More…, or "All
Properties" in the palette) shows every component and field, adding and
removing components, instance overrides with reset, and the components derived
from them; several selected objects share one sheet
(`docs/editor/interaction-grammar.md` §7). A brush's modifiers are added from a
menu of every kind there is, each saying what it does.

Every edit from the wheel, a handle or the sheet runs begin / preview / commit / cancel inside
the document's transaction, so undo, Esc and interruptions behave like a
viewport drag.

## Explorers

**Materials** (M, or the cartridge): a tray across the bottom of the world —
thumbnail grid, search, filters. Hovering a tile previews it on the selected
faces; leaving cancels the preview. Clicking loads the cartridge. With faces
selected, Enter or double-click applies to them.

**Prefabs** (P for the Entity tool, or the cartridge): a tray of scene
thumbnails. Picking one loads the cartridge and arms the Entity tool: a ghost of
the prefab, as it will be placed and lit as the scene is, follows the surface
under the cursor, or the grid where there is none, snapped to the grid lines
either way; a click places it as shown.
Pressing on the spot and dragging away turns it to face the pointer, by the
card's turn step while snapping is on, and lets go to place it; the next
placement starts facing the same way, and Esc during the turn puts it back.
Pressing a tile and dragging into the world is the same gesture — once the
tile leaves the tray, the tray tucks away, the Entity tool takes the prefab
and its ghost follows the pointer from view to view; letting go places it as
shown, and letting go off the views places nothing. An Empty or a component
shows, where it will stand, the marker it will be seen by, and turns the same
way. How it works: `docs/editor/placement.md`.

The Entity tool's fan on the tool wheel holds **Prefab**, **Empty** (a bare
entity named Entity: a transform, an identity and a name), and every component that
declares itself placeable (`SENCHA_PLACEABLE`, pictured by `SENCHA_PLACE_ICON`):
the engine's **Camera**, **Point Light** and **Spot Light**, and whatever a game adds, such
as the template's **Player Start**. Those place a new entity carrying just that
component at its defaults, named after it and selected, as one step; no prefab
is in hand for them.
Placement is tooling metadata the runtime ignores. `ComponentPlacement<T>`
carries it from the generated component definition through
`IComponentSerializer::Placement`, so the editor names no component type and a
game module's components offer themselves exactly as the engine's do. The icon
is the name of one of the tool's icons; the component code generator refuses
`SENCHA_PLACE_ICON` on a component that is not `SENCHA_PLACEABLE`.

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
| Place prefab | cartridge → tray → click world, or drag a tile into the world | P |
| Make a trigger volume | select a brush → Space → Make Trigger Volume; its responses in the Trigger group (`docs/gameplay/responses.md`) | palette: "Make Trigger Volume" |
| Make a mesh solid | select it → Add Component → Collider: a box around the mesh, drawn green (a sensor's cyan) where its body will be; Shape → Fit to Mesh refits it; a selected box resizes by its handles. Sizes are in the entity's frame and scale with it, as the mesh does | — |
| Turn a placement | press on the spot, drag away, release | Esc cancels |
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
