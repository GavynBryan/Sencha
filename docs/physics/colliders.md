# Collider geometry and body origins

`Collider.Shape` describes a primitive; `ShapePose` places it relative to the
body's origin. `RigidBodyBinding` copies both into `BodyDesc`. The backend uses
Jolt's rotated/translated shape decorator when the pose differs from identity.
The identity case keeps its existing shape without another allocation.

`GetBodyTransform`, `SetBodyTransform`, `TeleportBody`, driven-pose attachment
frames and ECS transforms still refer to the body origin. Shape offset does not
move that origin. Linear velocity is world-space center-of-mass velocity. Driven
constraints convert attachment-frame velocities using the actual center-of-mass
lever arm, including their teleport path. Jolt owns the resulting center of mass; sensor queries use its
center-of-mass transform, while movement sampling retains the body origin.
Continuous translation still casts the shape; teleportation resets that origin
and samples destination occupancy. Angular sweep limitations are unchanged.

A valid runtime mesh handle takes precedence over the primitive. An absent
shape cache or unresolved handle refuses body creation rather than substituting
a box. Primitive dimensions must be finite and positive (a capsule's cylinder
half-height may be zero); shape poses require finite positions and unit
quaternions. Query shape construction uses the same primitive validation.

## Scene contract

Collider now has the `collider` scene key and `CLDR` chunk. Its primitive,
`is_trigger` flag and `shape_pose` serialize through the ordinary component
serializer and shared archives. Missing fields use the C++ defaults: a unit-half-
extents box, solid collision and identity shape pose. Example:

```json
{
  "collider": {
    "shape": {"type": "Box", "half_extents": {"x": 1, "y": 2, "z": 3}},
    "is_trigger": true,
    "shape_pose": {
      "position": {"x": 3, "y": 0, "z": 0},
      "rotation": {"x": 0, "y": 0, "z": 0, "w": 1}
    }
  }
}
```

`SceneComponentValidation<T>` checks a value at the shared serializer boundary
before saving and after decoding, before installing it. The default compiles to
no additional validation. Collider uses it to reject invalid geometry and poses.
A process-local `Mesh` cache handle cannot be persisted; saving one, or loading
an authored `mesh` field, reports an error. Static brush mesh collision continues
through the existing `.scol`/collision-cell path, with its existing cache owner.

The user approved this SDK layout and new collider scene contract on 2026-10-19.
Collider and BodyDesc layouts changed; modules must rebuild. The existing ABI
fingerprint includes the affected headers. Existing component identities,
replication fields and scene scalar encodings retain their contracts. The new
collider key/chunk/fingerprint is frozen in the identity test table. Existing
scene containers need no version bump for an additional component type.

## Brush-derived sensors

A Trigger on a visible Volume brush cooks to a Collider with `IsTrigger=true`.
The brush remains the only editable geometry. A separately authored Collider on
that brush is an error, preventing two competing shape definitions.

The cook evaluates modifiers with the hard cook policy and requires one closed
six-quad oriented box. It applies entity scale and piece placement in body space, checks corners,
adjacency, closure and perpendicular edges, and derives the sensor dimensions
and shape pose. World translation and rotation remain on the body, avoiding
loss of geometric precision at distant world coordinates. Unsupported shapes, open meshes, arrays,
generated shells and affine shear fail with the authored entity's identity and
an actionable geometry diagnostic. No AABB approximation is used.

The entity keeps its transform, identity, parentage and trigger axes. World scale
is incorporated in the cooked shape once; body binding reads position/rotation
and does not apply scale again. A new cook and fresh binding are required after
shape or scale edits. Live collider configuration reconciliation remains in
[deferred work](../deferred.md#runtime-collider-configuration-reconciliation).

Runtime-bearing brushes, explicit trigger binding targets and their hierarchy
ancestors survive the scene projection. Ancestors and geometry-only referenced
brushes retain their transforms/identities without adding a second geometry
representation. Unrelated geometry-only brushes remain folded into static cook
cells. Solid rendering, collision cells and navigation still collect Solid
pieces only; the trigger's volume contributes none of them.

The authored source preserves every disabled and unknown row. The runtime cook
removes disabled command/condition rows from its execution program before the
existing generic asset dependency scan. Thus an unavailable asset in a disabled
unknown command does not become a required preload. Active rows are validated
against the editor's loaded Verb/Query vocabulary before any artifact is written.
The cooked program compiles against the runtime World's catalogs into its
World-owned store before occurrence processing; process-local dispatch IDs are
never persisted.

Cook projection and box extraction run during document collection, not per tick.
Projection uses full generational entity keys and linear retained-ancestor
closure. Shape extraction has fixed eight-vertex/six-face work per box. Physics
observations retain their cached shapes and bounded pair storage; no new
per-observation allocation or spatial index is introduced.

## Verification

The integrated collider/cook stack passed the dev configure and full build with
warnings as errors. Serial CTest reported zero failures across 6,182 enabled
entries in 451.65 seconds (17 opt-in generators skipped, one additional test
disabled). All 114 physics tests passed, as did the focused collider archive,
component identity, allocation, cook and hierarchy cases.

The new driven-frame regression was executed with the old origin-based velocity
calculation and failed in both normal and teleport cases; with the center-of-
mass lever arm it passes. The distant-coordinate shape regression similarly
failed with world-space fitting and passes with body-space fitting. These are
local results; remote CI is separate and its configuration prerequisite is #180.
