#pragma once

#include "IMeshEditTarget.h"
#include "MeshElementKind.h"

#include "../selection/ISelectionContext.h"

#include <math/geometry/3d/Plane.h>
#include <math/geometry/3d/Transform3d.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>

class LoggingProvider;

enum class MeshEditVerb : uint8_t
{
    Extrude,
    Delete,
    Clip,
    ResizeFace,
    TranslateElements,
    InsertEdgeLoop,
    FlipFaceNormal,
    RecalculateNormals,
};

struct MeshEditParams
{
    float Distance = 1.0f;
    Plane ClipPlane = {};
    bool KeepPositiveSide = false;
    float PlanePosition = 0.0f;
    float MinThickness = 0.1f;
    Vec3d TranslateDelta = {}; // world-space, for MeshEditVerb::TranslateElements
    float CutPosition = 0.5f;  // 0..1 along the seed edge, for InsertEdgeLoop
    bool LoopCut = true;       // InsertEdgeLoop: full loop (true) or single edge (false)
};

class MeshEditService
{
public:
    MeshEditService() = default;
    // Logging is optional: verbs that refuse an edit report why through this
    // provider. Tests default-construct without one and the reports become no-ops.
    explicit MeshEditService(LoggingProvider& logging);

    [[nodiscard]] MeshElementKind GetElementKind() const;
    void SetElementKind(MeshElementKind kind);
    MeshElementKind CycleElementKind();

    [[nodiscard]] std::unique_ptr<ICommand> ApplyVerb(IMeshEditTarget& target,
                                                      const SelectionSnapshot& selection,
                                                      MeshEditVerb verb,
                                                      const MeshEditParams& params = {}) const;

    // Moves the local vertices referenced by `elements` (interpreted per `kind`)
    // by a world-space delta, deduplicating shared vertices and converting the
    // delta through `transform` into local space. When `validate` is set the
    // result is run through BrushValidateAndRepair and nullopt is returned if the
    // geometry is unusable; otherwise the unvalidated mesh is returned, which is
    // intended for live drag preview. Object-mode translation must NOT use this —
    // it moves the entity transform via a command instead.
    [[nodiscard]] std::optional<BrushMesh> TranslateElements(const BrushMesh& base,
                                                             const Transform3f& transform,
                                                             std::span<const SelectableRef> elements,
                                                             MeshElementKind kind,
                                                             Vec3d worldDelta,
                                                             bool validate) const;

    // Rotates the local vertices referenced by `elements` (per `kind`) by `radians`
    // about the world axis `axis` through the world `pivot`. Like TranslateElements,
    // each vertex is taken to world through `transform`, rotated, and returned to
    // local; shared vertices move once. `validate` as in TranslateElements (false
    // for live preview, true to commit). Object-mode rotation must NOT use this; it
    // composes the entity transform instead.
    [[nodiscard]] std::optional<BrushMesh> RotateElements(const BrushMesh& base,
                                                          const Transform3f& transform,
                                                          std::span<const SelectableRef> elements,
                                                          MeshElementKind kind,
                                                          Vec3d axis,
                                                          double radians,
                                                          Vec3d pivot,
                                                          bool validate) const;

    // Scales the local vertices referenced by `elements` (per `kind`) by the
    // per-axis world `factor` about the world `pivot`. Same world round-trip and
    // `validate` contract as RotateElements. Object-mode scale composes the entity
    // transform instead.
    [[nodiscard]] std::optional<BrushMesh> ScaleElements(const BrushMesh& base,
                                                         const Transform3f& transform,
                                                         std::span<const SelectableRef> elements,
                                                         MeshElementKind kind,
                                                         Vec3d factor,
                                                         Vec3d pivot,
                                                         bool validate) const;

    // The extruded mesh plus the ids of the newly created "outer" elements in it
    // (the new cap faces for a face extrude, the new edges for an edge extrude),
    // so the drag can leave them selected for a follow-up edit. Ids index the same
    // space SelectableRef uses (face index / MeshElements edge index).
    struct ExtrudeResult
    {
        BrushMesh                  Mesh;
        std::vector<std::uint32_t> NewElementIds;
    };

    // Extrudes the elements referenced by `elements` (faces or edges, per `kind`)
    // by a world-space delta: faces grow a new cap + side walls, edges pull a new
    // quad plane, both offset along the (axis-constrained) drag vector. The delta
    // is converted to local through `transform`. Always repairs the result (an
    // unwelded extrusion is not renderable); returns nullopt if unusable. Used by
    // the gizmo Shift-drag extrude. `validate` is accepted for signature symmetry
    // with TranslateElements but extrude repairs regardless.
    [[nodiscard]] std::optional<ExtrudeResult> ExtrudeElements(const BrushMesh& base,
                                                               const Transform3f& transform,
                                                               std::span<const SelectableRef> elements,
                                                               MeshElementKind kind,
                                                               Vec3d worldDelta,
                                                               bool validate) const;

    // Remaps every vertex from the world AABB [oldMin,oldMax] to [newMin,newMax]
    // per axis (affine), so resizing the bounds scales the brush about the fixed
    // anchor. Degenerate (zero-extent) axes are left untouched. Vertices are
    // converted back to local through `transform`. `validate` as in
    // TranslateElements. The object-mode bounds-resize handles use this.
    [[nodiscard]] std::optional<BrushMesh> ResizeBounds(const BrushMesh& base,
                                                        const Transform3f& transform,
                                                        Vec3d oldMin, Vec3d oldMax,
                                                        Vec3d newMin, Vec3d newMax,
                                                        bool validate) const;

private:
    MeshElementKind ElementKind = MeshElementKind::Object;
    LoggingProvider* Logging = nullptr;
};
