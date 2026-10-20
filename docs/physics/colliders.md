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

## Verification

The integrated collider/cook stack passed the dev configure/full build with
warnings as errors and serial CTest: zero failures across 6,182 enabled entries
in 451.65 seconds (17 opt-in generators skipped; one additional test disabled).
All 114 physics tests and the focused collider archive, identity and allocation
tests passed. Driven-frame regressions fail with the original origin-based
velocity calculation and pass with the actual center-of-mass lever arm.
Brush-derived sensor cooking is the following review unit.
