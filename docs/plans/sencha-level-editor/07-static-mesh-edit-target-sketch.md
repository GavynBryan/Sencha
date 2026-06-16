# Static Mesh Edit Target — Seam Sketch

Status: design sketch (Phase D, deliverable 5). No code in this pass.

## Purpose

Prove that adding a second editable target (a static mesh) is **target-only**
work: it requires implementing `IMeshEditTarget` and nothing else. If that holds,
the mesh-edit subsystem's central bet — a generic edit service over a target
seam — is sound. This document maps a hypothetical `StaticMeshEditTarget` onto
the existing seam and is explicit about the one assumption that must hold.

## The seam as it exists today

```cpp
struct MeshEditTargetMesh {
    const BrushMesh* Mesh = nullptr;
    Transform3f      Transform = Transform3f::Identity();
};

struct IMeshEditTarget {
    virtual std::optional<MeshEditTargetMesh> Resolve(EntityId) const = 0;
    virtual std::unique_ptr<ICommand> MakeEditCommand(EntityId, BrushMesh before, BrushMesh after) = 0;
};
```

Everything above the seam is target-agnostic and already routes only through it:

- `MeshEditService` — verbs (`Extrude`/`Delete`/`Clip`/`ResizeFace`/
  `TranslateElements`), validation via `BrushValidateAndRepair`, command
  creation via `target.MakeEditCommand`.
- `MeshElements` — face/edge/vertex projection from a `BrushMesh` + `Transform`.
- Picking (`PickingService`) — by element mode, from `MeshElements`.
- `MeshEditSession` + `TranslateGizmo` (`IGizmo`) — pivot, hit-test, drag,
  handlers that call `MeshEditService::TranslateElements` and commit via the
  target.
- `SelectionRenderer` — selection highlights + gizmo, all from `MeshElements`.

None of these name `BrushEditTarget`, `LevelScene`, or a brush component. They
depend on `IMeshEditTarget`, `BrushMesh`, `Transform3f`, and `SelectableRef`.

## What `StaticMeshEditTarget` would implement

```cpp
class StaticMeshEditTarget : public IMeshEditTarget {
public:
    StaticMeshEditTarget(LevelScene& scene, LevelDocument& document);

    std::optional<MeshEditTargetMesh> Resolve(EntityId entity) const override {
        // Resolve the entity's editable static-mesh geometry to a BrushMesh-shaped
        // view (indexed face-vertex) + its world transform. Either the component
        // stores an editable BrushMesh sidecar, or this adapts the runtime mesh
        // into one and caches it.
    }

    std::unique_ptr<ICommand> MakeEditCommand(EntityId entity, BrushMesh before, BrushMesh after) override {
        // Return an EditStaticMeshCommand storing before/after, which on
        // execute/undo writes the editable mesh back onto the component and
        // re-derives the render/cook mesh.
    }
};
```

Wiring is one line at the editor seam, mirroring `BrushEditTarget`: the session
already resolves edits through *an* `IMeshEditTarget`; selecting which target to
use is a lookup on the entity's components (brush vs static mesh), not a change
to any generic code.

## What does NOT change

- `editor/meshedit/` — zero changes. (The dependency-check ctest,
  `meshedit_dependency_directions`, enforces this.)
- Picking, selection, gizmo, session flow, renderer — zero changes; they operate
  on `MeshElements`/`BrushMesh`, which the target supplies.
- `MeshEditVerb` set and parameter structs — unchanged.

## The one assumption (stated honestly)

`MeshEditService` is typed on `BrushMesh` and calls `BrushOps`/
`BrushValidateAndRepair` directly. So the seam is target-only **iff a static
mesh's editable geometry is representable as a `BrushMesh`** (closed/orientable
indexed face-vertex) and round-trips through it on commit.

This is the intended reading of non-goal #1 ("do not rename `BrushMesh` to a
generic `EditMesh` yet"): `BrushMesh` is the de-facto edit-mesh format, and a
static mesh target adapts to/from it. Consequences:

- Triangle soup with no consistent winding won't `BrushValidateAndRepair`
  cleanly; the target's `Resolve` must hand over a repaired, weldable mesh (or
  reject non-editable meshes by returning `nullopt`).
- Per-face/loop authoring data the static mesh cares about (UVs, smoothing
  groups) lives outside `BrushMesh` today and would be the target's concern to
  preserve across an edit, not the service's.

If a future static mesh needs an editable representation that is *not*
`BrushMesh`-shaped, the localized change is to rename `BrushMesh` →
`EditMesh` and template/abstract `MeshEditService` over the verb kernel — still
not a change to picking, selection, gizmo, session, or renderer. That rename is
the only forward-looking risk, and it stays inside `editor/meshedit/` +
`editor/level/brush/`.

## Conclusion

Adding a static mesh editor is target-only work (`StaticMeshEditTarget` +
`EditStaticMeshCommand` + one wiring lookup) **provided** its editable geometry
round-trips through `BrushMesh`. The generic subsystem needs no changes, and the
dependency-direction ctest guards that boundary mechanically.
