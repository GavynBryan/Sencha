#pragma once

#include "IManipulator.h"

// AABB resize handles for object mode: a ball at each of the six face centers, on
// a dotted axis from the box center. Dragging a ball moves that face along its
// world axis (grid-snapped, min-thickness clamped) about the opposite side,
// scaling the brush's vertices via MeshEditService::ResizeBounds. Applies in both
// ortho and perspective views; single-brush selection. The default gizmo (Resize).
class BoundsManipulator : public IManipulator
{
public:
    [[nodiscard]] TransformMode Mode() const override { return TransformMode::Resize; }
    [[nodiscard]] bool AppliesTo(const ManipulatorContext& ctx,
                                 const EditorViewport& viewport) const override;
    void BuildVisual(const ManipulatorContext& ctx,
                     const EditorViewport& viewport,
                     int hoveredPart,
                     ManipulatorVisual& out) const override;
    [[nodiscard]] int HitTest(const ManipulatorContext& ctx,
                              const EditorViewport& viewport,
                              ImVec2 screenPos) const override;
    [[nodiscard]] std::unique_ptr<IInteraction> BeginDrag(
        int part,
        const ManipulatorContext& ctx,
        const EditorViewport& viewport,
        ImVec2 screenPos,
        ModifierFlags modifiers) const override;
};
