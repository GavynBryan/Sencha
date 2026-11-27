# Link Data and Authoring

Status: proposed execution specification, revision 2 (2026-11-27). Owns Stage 1 with the
geometry kernel in `02`.

This document defines the authored data for a link, how it resolves at runtime, what the
cook emits and checks, and how a designer makes one.

## 1. What exists and is reused

| Mechanism | Where | Used for |
|---|---|---|
| `WorldLink { Id, ZoneA, ZoneB, Kind, Directions }` | `engine/include/zone/WorldConnectionComponents.h` | The link. Gains endpoints. |
| `WorldDock` with `Vec2d HalfExtents`, its adapter and handles | same header; `editor/level_editor/src/authoring/WorldDockEditorAdapter.cpp` | The model for the aperture's rectangle, overlay and edge handles. |
| `PersistableRef`, `PersistentEntityIndex`, `PendingReferences` | `engine/include/world/identity/` | Durable endpoint references and their lookup. |
| World scene loaded into the persistent partition | `LoadedLevel::LoadWorld` in `engine/src/app/LoadedLevel.cpp` | Where the link entity lives at runtime. |
| `CookWorldTopology`, `LinkEndpoint` in the manifest | `engine/src/zone/WorldTopologyCook.cpp`, `engine/include/zone/WorldPartitionManifest.h` | Cooked link topology. Gains aperture frames. |
| `ValidatePersistentIds` over every zone and the world scene | `editor/level_editor/src/document/WorldCook.cpp` | Why an id needs no zone qualifier. |
| `WorldLinkRecipe`, the "Teleport Link To" verb | `editor/level_editor/src/document/EditorEntityRecipe.cpp`, `workspace/PartitionOutline.cpp` | The existing way a link is created. |
| `MoveEntitiesToZoneCommand` | `editor/level_editor/src/document/commands/` | Precedent for one command writing two documents. |
| Component codegen and `ComponentSet` registration | `engine/include/ecs/ComponentAnnotations.h`, `docs/core-systems-map.md` | How the new component is added. |

Nothing here adds a scene-loading path, a reference-resolution path or a second link
type.

## 2. Authored data

### 2.1 Aperture

```cpp
struct SENCHA_COMPONENT("Link Aperture") SENCHA_SCHEMA("Link Aperture")
       SENCHA_SCENE_CHUNK("LAPT") SENCHA_PLACEABLE
LinkAperture
{
    SENCHA_FIELD("half_extents")
    Vec2d HalfExtents = Vec2d(1.0, 2.0);
};
```

The frame is the entity's transform. The component holds nothing else: no partner, link
id, matrix, zone, render target, physics handle, crossing state or enabled flag.

It is an engine component, cooked into the zone that holds it, and follows the
`LevelLandmark` pattern (`engine/include/world/LevelLandmark.h`): the header, one line
in `SENCHA_ENGINE_COMPONENT_HEADERS`, one line in a `ComponentSet`. Defaults are member
initializers. The identity string is frozen once content exists.

### 2.2 Link

`WorldLink` gains two fields:

```cpp
    SENCHA_FIELD("endpoint_a")
    PersistableRef EndpointA;

    SENCHA_FIELD("endpoint_b")
    PersistableRef EndpointB;
```

Both null: a demand-only link, as today. Both set: a spatial link. One set: invalid.

`ZoneA` and `ZoneB` stay. The manifest needs them when neither zone is resident, and
demand-only links have nothing else. For a spatial link they are derived from the
apertures' owning zones: the editor command writes them in the same transaction as the
references, and cook fails when they disagree. Nobody edits them by hand on a spatial
link.

`Directions` keeps its meaning. A one-way link is traversable, visible and transmitting
in its authored direction only; the reverse aperture is the fallback surface.

The link's `LinkId` is its stable identity for cook, diagnostics, deterministic ordering
and render key scope. It is not a runtime id and is safe to persist.

### 2.3 Format consequences

- `WLNK` scene chunk: two added fields, absent means null. Old scenes load unchanged.
- `LinkEndpoint` in the cooked manifest: gains each aperture's origin, normal, right, up
  and half extents, the same shape `DockEndpoint` already has. Additive.
- `LAPT`: a new chunk.

This is decision D2 and needs the owner's ruling before Stage 1. If the module ABI
fingerprint moves, update it and its compatibility tests in the same change.

Verify at stage start: whether `CookWorldTopology` accepts a link whose `ZoneA` equals
`ZoneB`. Same-zone links are required. If it refuses, allowing it is part of Stage 1.

## 3. Runtime resolution

### 3.1 The resolved record

`ResolvedLinks` is a World resource. It publishes one ordered span, immutable for a
fixed tick and for a render extraction:

```cpp
struct ResolvedLink
{
    LinkId Id;
    EntityId Link;
    EntityId ApertureA;
    EntityId ApertureB;
    ZoneId ZoneA;
    ZoneId ZoneB;
    ApertureFrame FrameA;      // origin, normal, right, up, half extents
    ApertureFrame FrameB;
    Transform3d AToB;
    Transform3d BToA;
    std::uint32_t Directions;
    bool Upright;
};
```

It owns no entities, GPU objects, physics bodies or leases.

### 3.2 States

Kept as a plain enum per link, with transitions in one function:

- `Active`: both apertures resident and valid.
- `AwaitingA`, `AwaitingB`, `AwaitingBoth`: a zone is not resident. Normal, not an error.
- `DemandOnly`: no endpoints authored.
- `Invalid`: one endpoint only, a reference that names something without `LinkAperture`,
  a bad frame, or an aperture claimed by two links. Diagnosed once per transition.

Only `Active` links appear in the span.

### 3.3 When it updates

There are no resolution epochs. The table rebuilds affected records when:

- a zone publishes or detaches (the residency finalisation that already drains partition
  moves in `RuntimeWorld`);
- a `WorldLink` or `LinkAperture` is added, removed or changed (cached changed queries);
- a level loads or unloads.

A cached `EntityId` is checked with `IsAlive` and re-resolved through
`ResolvePersistableRef` when dead. Generational ids make a stale hit impossible, so
revision 1's partition-slot-reuse cases do not exist.

Output is sorted by `LinkId`. The update runs on the frame thread at a fixed point; no
worker writes the table.

With no `WorldLink` carrying endpoints, the update is one empty cached query.

### 3.4 Frame convention

Pinned by tests before any consumer exists:

- origin is the aperture's world position;
- the front normal is the entity's forward;
- aperture coordinates are right and up;
- front has positive signed distance;
- traversal enters through the front of one aperture and leaves through the front of the
  other, so the mapping includes a half turn about the aperture's up axis.

### 3.5 Valid aperture

Finite translation, normalisable rotation, unit scale within a named tolerance, both
half extents finite and positive. The endpoint is static through v1.0: a transform
change at runtime invalidates the record and is diagnosed.

`Upright` is true when `AToB` maps world up to world up within a named tolerance.

## 4. Cook

### 4.1 Collection

`CollectWorldCookInput` already loads every zone document and the world scene. It
additionally collects, per spatial link: the link id, both references, each aperture's
world frame and half extents, each aperture's owning zone, and `Directions`.

### 4.2 Validation

Reported sorted by link id then rule:

- exactly one endpoint set;
- a reference to a missing entity, or to one without `LinkAperture`;
- both references naming the same aperture;
- an aperture claimed by two links;
- an invalid frame or extent;
- half extents that differ between the two apertures beyond tolerance;
- `ZoneA`/`ZoneB` disagreeing with the apertures' zones;
- the two zones' content bounds overlapping without a dock between them (D1);
- a recess shallower than a registered character profile needs (D6), as a warning that
  names the profile;
- a non-upright link, as a note that characters cannot use it (D4);
- a `LinkAperture` in a zone with no link naming it, as a warning.

### 4.3 Output

- `LinkEndpoint` frames in the manifest.
- Per zone, an in-zone navigation link pair for each traversable direction of a
  same-zone upright link (`06`, section 6).
- Per zone lighting inputs for `05`, when that stage lands.

Cook order, document load order and manifest order do not change the output bytes.

### 4.4 Cook hash

A zone's fingerprints include a link only where it can change that zone's output: the
topology fingerprint always, the navigation fingerprint for same-zone links, and the
lighting fingerprints from Stage 12. Editing an unrelated link restales nothing.

## 5. Editor

### 5.1 Prerequisite: an entity-reference property

The inspector has no property kind for "another entity". A `PersistableRef` field shows
as unsupported, `HeldEditorAdapter` hand-builds a choice list over the focused document,
and a verb's entity argument is a text box for sixteen hex digits. That is three
consumers of one missing mechanism.

Stage 1 adds an entity-reference property kind to `editor/common/src/properties/`:

- it presents the target by name and zone, with pick, clear and select-target;
- its candidate list is a filter supplied by the field (here: entities with
  `LinkAperture` not already claimed), over every loaded document of the world, not
  only the focused one;
- a target in a document that is not loaded shows as "in zone X, not loaded", not as an
  error.

`HeldEditorAdapter` and the verb argument row move to it in the same change, so the
hand-written picker is deleted and not left beside the new one.

### 5.2 Making a link

A focus change clears the command stack, so the pair operation must not change focus
partway. It runs with the world scene focused and writes the zone documents directly,
as `MoveEntitiesToZoneCommand` does.

One composite command, one undo step:

1. create an aperture entity in each named zone at the given frames;
2. create the `WorldLink` with both references and derived zones;
3. validate.

It is offered as:

- a level operation, `link.place_at`, that fits each aperture to an existing opening the
  way `dock.place_at` fits a dock, for the agent-facing operation list;
- a verb on a selected opening, "Link To Opening...", which picks the second opening in
  any loaded zone.

Placing a lone `LinkAperture` from the Entity tool's fan stays possible (it is
`SENCHA_PLACEABLE`) and produces the unlinked-aperture warning until a link names it.

### 5.3 Editing

The link's sheet shows both endpoint pickers, swap, select endpoint, and the validation
state. The aperture's sheet shows half extents and a read-only "linked to" row that
selects the link. Rectangle handles come from the same `PropertyHandles` rule rows the
dock uses; if the dock's rows cannot be shared as they are, extract the shared part in
this change.

Resizing one aperture resizes its partner in the same command, since unequal extents are
invalid.

### 5.4 Duplicate, delete, move

- Duplicating an aperture copies the component and creates no link.
- Duplicating a link alone is refused: it would claim the same apertures.
- "Duplicate pair" duplicates both apertures and creates a new link with a fresh
  `LinkId`, through the existing `RemapWorldConnectionDuplicateSnapshots` path.
- Deleting an aperture leaves the link invalid and visibly repairable. "Delete pair"
  removes all three.
- Moving an aperture to another zone rewrites the link's zone field in the same command.
  The reference itself does not change, because ids are world-unique.

### 5.5 Viewport

Through an `EditorComponentAdapter`, never a branch in generic code:

- the aperture rectangle, its front normal, and its recess depth check;
- a line to the partner when both documents are loaded;
- state colouring for linked, unlinked, invalid and not-upright;
- selecting the link from either aperture.

The viewport does not draw the linked view. That is seen in play-in-editor. Drawing it
in the viewport is deferred (`00`, section 9).

### 5.6 Reachability

`world.reachable` lists links as `Unconsidered` because they have no positions. With
frames in the cook input it can treat a spatial link as a crossing with an entry and an
exit. Doing so is part of Stage 1, since the data arrives here and the consumer is
already waiting.

## 6. Demand

A link is already a one-hop edge in `ComputeZoneDemand`: with the focus in one zone, the
linked zone is resident with neighbour participation. Stage 1 adds no demand code.

Verify at stage start: that the configured `NeighborVisible` and `NeighborPhysics`
participation is what a linked view and a traversal need, and what happens when
`HopCount` is zero. Straddle pinning is in `02`.

## 7. Debug

- `ResolvedLinks` counters: active, awaiting, invalid, rebuilds this frame.
- Debug draw: aperture rectangles, normals, link id, state, the mapping applied to a
  marker.
- A console listing of links and their states.

## 8. Tests

- `LinkApertureSerializationTests`: round trip, defaults, old `WLNK` data with no
  endpoint fields.
- `ResolvedLinksTests`:
  1. both resident resolves `Active`;
  2. either zone absent resolves the matching `Awaiting` state and is not diagnosed;
  3. unload then reload resolves again to new live ids;
  4. world scene loaded before and after the zones gives the same ordered span;
  5. one aperture claimed twice invalidates both links deterministically;
  6. the zero-link update touches no entity.
- `ApertureFrameTests`: the exact mapping matrix, its inverse, handedness, `Upright`.
- `WorldLinkCookTests`: every validation rule; byte-identical output across load order;
  an unrelated link edit restales nothing.
- `LinkEditorCommandTests`: create pair, swap, resize both, duplicate pair, delete pair,
  move aperture between zones; each undoes and redoes as one step with the world scene
  focused.
- `EntityReferencePropertyTests`: pick across documents, unloaded target display, and
  `Held` still editable through the shared kind.
- No authored or cooked output contains an `EntityId` or storage partition id.
