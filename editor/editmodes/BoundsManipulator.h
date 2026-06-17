#pragma once

#include "IManipulator.h"

// Hammer-style AABB resize handles for object mode in the orthographic views:
// white squares at the selected brush's bounding-box corners and edge-midpoints.
// Dragging a handle resizes the box (grid-snapped, min-thickness clamped) about
// the opposite side, scaling the brush's vertices via MeshEditService::ResizeBounds.
// Applies only in ortho views (where corner drag is unambiguous); the perspective
// gizmo handles movement. Single-brush selection. (08-select-tool-v2.md P3.)
class BoundsManipulator : public IManipulator
{
public:
    [[nodiscard]] bool AppliesTo(const ManipulatorContext& ctx,
                                 const EditorViewport& viewport) const override;
    void BuildVisual(const ManipulatorContext& ctx,
                     const EditorViewport& viewport,
                     ManipulatorVisual& out) const override;
    [[nodiscard]] int HitTest(const ManipulatorContext& ctx,
                              const EditorViewport& viewport,
                              ImVec2 screenPos) const override;
    [[nodiscard]] std::unique_ptr<IInteraction> BeginDrag(
        int part,
        const ManipulatorContext& ctx,
        const EditorViewport& viewport,
        ImVec2 screenPos) const override;
};
