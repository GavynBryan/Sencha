#pragma once

#include "PivotState.h"
#include "TransformMode.h"

#include "interaction/IInteraction.h"
#include "meshedit/MeshElementKind.h"
#include "selection/ISelectionContext.h"
#include "viewport/GridSettings.h"

#include <math/Vec.h>

#include <imgui.h>

#include <array>
#include <memory>
#include <vector>

struct EditorViewport;
class MeshEditService;
struct ManipulationSink;

// What a manipulator needs to query the scene and drive an edit, all through
// generic seams (no EditorScene). The current element mode lives on the service.
struct ManipulatorContext
{
    const SelectionSnapshot& Selection;
    MeshEditService& Service;
    ManipulationSink& Sink;
    // Non-const like Pivot: the Move gizmo's grid-origin drag writes Origin.
    GridSettings& Grid;
    // Read by every manipulator (via ComputeSelectionPivot); written only by the
    // Move gizmo's pivot-edit drag. A reference member, so the pivot-edit apply can
    // update it even though the context is passed const.
    PivotState& Pivot;
    // The gizmo frame: the axis directions the translate/rotate gizmos draw and
    // drag along, resolved by the session from the active TransformSpace
    // (world / grid frame / primary selection's local axes). Always orthonormal.
    std::array<Vec3d, 3> Axes = { Vec3d{ 1, 0, 0 }, Vec3d{ 0, 1, 0 }, Vec3d{ 0, 0, 1 } };
    // The Move gizmo targets the grid origin instead of the selection (the
    // grid-frame analog of pivot editing).
    bool EditGridOrigin = false;
};

// A manipulator's drawable geometry. Lines now; P3 adds screen-constant handle
// quads. The overlay renderer draws whatever is here, so adding a manipulator
// never edits the renderer.
struct ManipulatorVisual
{
    struct Line
    {
        Vec3d A = {};
        Vec3d B = {};
        Vec4 Color = {};
    };

    std::vector<Line> Lines;
};

// A manipulation gizmo/handle set (translate now; bounds/rotate/scale/clip later).
// The session routes pointer input to the first applicable manipulator that gets
// a hit; the renderer draws every applicable manipulator's visual. Adding one is
// a new file implementing this — no session/renderer/sink changes. Parts are
// opaque non-zero ids the manipulator interprets (0 == miss).
struct IManipulator
{
    // Which transform gizmo this is. The session shows and routes only the
    // manipulator(s) matching the active TransformMode, so gizmos are switchable.
    [[nodiscard]] virtual TransformMode Mode() const = 0;

    [[nodiscard]] virtual bool AppliesTo(const ManipulatorContext& ctx,
                                         const EditorViewport& viewport) const = 0;

    // `hoveredPart` is the part currently under the cursor (0 = none), so the
    // manipulator can brighten it; matches the part ids from HitTest/BeginDrag.
    virtual void BuildVisual(const ManipulatorContext& ctx,
                             const EditorViewport& viewport,
                             int hoveredPart,
                             ManipulatorVisual& out) const = 0;

    [[nodiscard]] virtual int HitTest(const ManipulatorContext& ctx,
                                      const EditorViewport& viewport,
                                      ImVec2 screenPos) const = 0;

    // `modifiers` carries the pointer-down modifier state (Shift/Ctrl/Alt) so a
    // manipulator can pick a drag variant (e.g. Shift = duplicate/extrude) without
    // the session decoding intent. 0-cost for manipulators that ignore it.
    [[nodiscard]] virtual std::unique_ptr<IInteraction> BeginDrag(
        int part,
        const ManipulatorContext& ctx,
        const EditorViewport& viewport,
        ImVec2 screenPos,
        ModifierFlags modifiers) const = 0;

    virtual ~IManipulator() = default;
};
