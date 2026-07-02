#pragma once

#include "IManipulator.h"

// 3-axis scale manipulator: a stalk with an end box per world axis plus a center
// box for uniform scale, drawn screen-constant around the selection pivot.
// Dragging an axis box scales that axis about the pivot; the center box scales all
// axes. Object mode scales the entity transform, element modes scale the selected
// vertices (MeshEditService::ScaleElements), both through the ManipulationSink.
class ScaleManipulator : public IManipulator
{
public:
    [[nodiscard]] TransformMode Mode() const override { return TransformMode::Scale; }
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
