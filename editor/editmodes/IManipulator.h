#pragma once

#include "../interaction/IInteraction.h"
#include "../meshedit/MeshElementKind.h"
#include "../selection/ISelectionContext.h"

#include <math/Vec.h>

#include <imgui.h>

#include <memory>
#include <vector>

struct EditorViewport;
class MeshEditService;
struct ManipulationSink;

// What a manipulator needs to query the scene and drive an edit, all through
// generic seams (no LevelScene). The current element mode lives on the service.
struct ManipulatorContext
{
    const SelectionSnapshot& Selection;
    MeshEditService& Service;
    ManipulationSink& Sink;
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

    [[nodiscard]] virtual std::unique_ptr<IInteraction> BeginDrag(
        int part,
        const ManipulatorContext& ctx,
        const EditorViewport& viewport,
        ImVec2 screenPos) const = 0;

    virtual ~IManipulator() = default;
};
