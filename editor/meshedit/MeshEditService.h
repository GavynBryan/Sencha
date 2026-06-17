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

enum class MeshEditVerb : uint8_t
{
    Extrude,
    Delete,
    Clip,
    ResizeFace,
    TranslateElements,
    SplitEdge,
};

struct MeshEditParams
{
    float Distance = 1.0f;
    Plane ClipPlane = {};
    bool KeepPositiveSide = false;
    float PlanePosition = 0.0f;
    float MinThickness = 0.1f;
    Vec3d TranslateDelta = {}; // world-space, for MeshEditVerb::TranslateElements
};

class MeshEditService
{
public:
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
};
