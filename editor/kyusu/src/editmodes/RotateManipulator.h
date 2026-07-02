#pragma once

#include "IManipulator.h"

// 3-axis rotate manipulator: a ring per world axis (X/Y/Z), drawn screen-constant
// around the selection pivot. Dragging a ring rotates about that axis through the
// pivot; object mode composes the entity transform, element modes rotate the
// selected vertices (MeshEditService::RotateElements), both through the
// ManipulationSink. Stateless; the pivot derives from context each call.
class RotateManipulator : public IManipulator
{
public:
    [[nodiscard]] TransformMode Mode() const override { return TransformMode::Rotate; }
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
