# Placement

How the Level workspace puts something down: a prefab, an empty entity, or a
new entity carrying one placeable component. The Entity tool (`PlaceTool`), a
file carried in from the shelf, and a file dropped on the views all go through
one pipeline, so what the views show before the placement is made is exactly
what making it writes.

## The pipeline

| Stage | Owner | Where |
| --- | --- | --- |
| What is under the pointer | `PickingService` (surface hit, grid point) | `viewport/Picking.h` |
| Where a placement belongs | `ResolvePlacementSite` | `document/placement/PlacementResolver.h` |
| The frame it turns in | `PlacementSite`, `PlacementTransform` | `document/placement/PlacementSite.h` |
| Facing the pointer | `PlacementTurn::FacingToward` | `document/placement/PlacementTurn.h` |
| The one pending placement | `PlacementSession` | `document/placement/PlacementSession.h` |
| Whether the document takes it | `PlacementRefusal` | `document/placement/PlacementCommand.h` |
| Making it, as one step | `MakePlacementCommand`, `PlacePending` | `document/placement/PlacementCommand.h` |
| Its marks | `AppendPlacementMarks` | `overlay/PlacementMarks.h` |
| Its ghost | `PlacementGhost`, `SceneSourceStage` | `render/` |

Picking only finds hits. Deciding where a placement belongs given those hits
is the resolver's: today, the nearest surface and otherwise the grid,
standing upright. On a surface it snaps across the surface at the surface's
own height, on the same lattice as the working grid (`GridThrough`, which the
brush tool's surface-rest creation uses too), so a placement on a raised
floor lands on grid lines just as one on the ground does.

## One pending value

`PlacementSession` lives in the workspace's per-document interaction state
(`WorkspaceInteractionRuntime`, reached by tools as `ToolContext::Placement`)
and is cleared with it: on a rebuild of the editing stack (document open,
focus change) and by `ClearTransient`. It holds a `PendingPlacement` --
subject, site, yaw, and the `Transform3f` computed from them once, when the
site or yaw changes -- and a phase:

| Phase | Meaning | Left by |
| --- | --- | --- |
| Idle | nothing pending | `Aim` |
| Aiming | the placement follows the pointer | `Withdraw`, `BeginTurn`, `Commit`, `Clear` |
| Turning | its site is held, its yaw follows the pointer | `CancelTurn`, `Commit`, `Clear` |

A transition that does not apply in the current phase does nothing; in
particular nothing re-aims a turn, so a turn keeps the subject and the site it
began with.

The ghost and the marks read the session; they never hold a copy. `Commit`
hands back the pending value exactly as it was shown, and `PlacePending`
builds the command from that value. Nothing re-targets on release: a click
commits what the hover settled at the press, a turn commits its last yaw, and
a carry commits where the pointer last was over a view.

The document judges a subject when it arrives in the session and again when
the command is built. `EditorDocument::CanPlaceSceneSource` is the owner of
the scene rules -- it resolves, and it is not the document's own source -- so
the Entity tool, a carry and a drop are refused alike. A refused placement is
marked in the warning colour and the tool's hint says why.

## Gesture lifecycle

`ViewportToolDispatcher` owns every left-button gesture over the views, from
its press, or its hand-off from the shelf, to its release:

1. A gesture belongs to the tool that took it. Another tool in hand ends it as
   Escape does.
2. While a button is held, nothing hovers: `ITool::OnHover` means no button.
3. Before a press starts a gesture, the tool's hover is brought to the press
   point, so a click or a drag acts on what was shown there.
4. A gesture ends exactly one way: committed by its release, or cancelled by
   Escape, focus loss (which a hidden workspace routes too), a tool switch, or
   a rebuilt editing stack. `WorkspaceInteractionRuntime::Rebuild` cancels the
   live interaction before the old context goes, because an interaction takes
   its context per call and would otherwise commit into the next document.
5. A pressed gesture is pinned: `PointerCapture::Acquire` with its view, so
   every later event is stamped with that view wherever the pointer goes, and
   a release outside it still commits. A handed gesture follows the pointer:
   captured with no view, each event is stamped with the view under the
   pointer, and over none the interaction hears `OnPointerLeftViews`; a
   release there cancels.

A press that was aborted, or a capture inherited from a rebuilt stack, ends
nothing when its release comes. A release the platform loses is recovered by
`PlatformEventRouter::ReleasesMissedBefore` before the router reaches the
workspace; the commit never reads the release's position, so a late release
cannot move what is made.

## Turning

Pressing on the spot and dragging past the deadzone begins a
`PlaceTurnInteraction`: the site holds, and the placement faces the point
where the pointer's ray meets the site's plane (`FacingToward`). Facing is
absolute -- the front looks at the pointer -- so there is no angle to
accumulate and no wrap to correct.

The turn holds the current yaw, rather than guessing, where that point is
ill-posed:

- the ray meets the plane at less than about 3 degrees
  (`PlacementTurn::kMinGrazingSine`): a front or side orthographic view, where
  every ray lies along the floor, or a perspective view near the horizon;
- the plane is behind the ray's origin;
- the point is within 12 pixels of the spot, measured at the spot's depth,
  where the direction is noise.

While the toolbar's snap is on, the yaw snaps to the nearest multiple of the
card's turn step, measured from the site's yaw zero (`GizmoMath::SnapAngle`);
off, it is free. A committed turn's yaw becomes the tool's resting yaw, so the
next placement starts facing the same way; a cancelled one turns back.

`PlacementSite` carries its own up and yaw-zero facing, and the transform is
the frame's rotation turned about up. Sites are upright today. Standing on a
surface's normal (`SiteOnPlane`, the card's planned "align to surface") gives
the turn a different plane and nothing else; the frame falls back from world
forward to world right when forward runs along the normal, so it depends on
the normal alone.

## The ghost is a placement

The ghost of a prefab is not an approximation of a placement: it is one.
`SceneSourceStage` places the prefab in a scratch document of its own with
`EditorDocument::PlaceSceneInstance`, the same call a level makes, so the same
instance projection expands it (nested prefabs, brushes, meshes, skinned
meshes, lights). Posing it sets the instance root's transform, as editing a
placement does; the scene's own transform propagation and the brush
evaluation policy the views use derive the rest; the same
`SceneRenderQueueBuilder` builds its queues. Its world transforms are equal,
bit for bit, to the placed prefab's (`SceneSourceStageTests`).

`PlacementGhost` keeps a stage per prefab recently in hand
(`editor.placement.prepared_prefabs`, default 4). Hovering and turning only
pose a stage. A prefab coming into hand is prepared again only when a source
it placed changed on disk (`SceneSourceCache::ChangedOnDisk`), or was saved
(`WorldDocument::OnSourcesSaved`, which also re-renders the scene previews).
Preparation is synchronous on the main thread, once per prefab version,
because loading a document leases its assets there. It resolves through the
focus document's content roots, so it places what the document would.

The ghost draws in every view after the placed meshes, through the forward
pass, with real materials under a light tint (`EditorTheme::PlacementGhostTint`),
lit by the focus scene's lights, depth-tested and depth-writing, followed by
its component visuals. It is outside every other system by construction: it
is in no open document, so it is never picked, selected, saved or undone; its
casters and its lights are gathered by its own builder and never merged, so it
casts no shadow and lights nothing. A prefab that does not place draws
nothing; its marks turn the warning colour. Meshes still loading appear as
they become resident, since the queues are built each frame.

Dropping a stage releases its meshes through the Vulkan deletion queue, so a
frame in flight keeps drawing them.

### What is shared with the scene previews

`SceneThumbnailCache` frames the same `SceneSourceStage` at the identity, so a
preview is what a placement makes. The stage is the only thing shared: the
previews keep their own framing and release a stage as soon as its passes are
recorded, the ghost keeps its stages while in hand. Imported meshes,
materials and textures are already shared by both through the asset caches.
Brush bakes are not: `BrushBakeCache` keys records per registry and sweeps
every registry that is not an open document, so a scratch stage keeps a
cache of its own. Both are invalidated by the same `OnSourcesSaved`.

## The shelf carry

A tile dragged off the shelf reports where it is (`shelf_carrying`). Once it
is over the content and the active workspace takes carries of its kind
(`IWorkspace::TakesCarry`), the shell takes the carried file off the shelf, so
the shelf's own drag ends without a drop, hands the rest of the gesture to the
application (`HandGestureToApplication`), tucks the tray away, and calls
`TakeCarry`. The Level workspace puts the prefab in hand, takes up the Entity
tool, and routes a `PointerHandedEvent`; the dispatcher takes a following
capture and asks the tool for `ITool::TakeHandedGesture`, which is
`PlaceCarryInteraction`. Its release commits through `PlacePending`.

A file let go above the shelf before the content could take it (a flick
within one frame) arrives as `DropAsset`; for a prefab that is the same carry,
handed over and released at the drop point. Materials do not carry; they drop.
