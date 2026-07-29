#pragma once

#include "IManipulator.h"

// Generic face/edge handles emitted by editor component adapters. The
// manipulator understands only AABBs and bounded rectangles; component meaning
// remains in the adapter-provided value transaction.
class AffordanceManipulator final : public IManipulator
{
public:
    [[nodiscard]] TransformMode Mode() const override { return TransformMode::Resize; }
    [[nodiscard]] bool AppliesTo(const ManipulatorContext& ctx,
                                 const EditorViewport& viewport) const override;
    void BuildVisual(const ManipulatorContext& ctx, const EditorViewport& viewport,
                     int hoveredPart, ManipulatorVisual& out) const override;
    [[nodiscard]] int HitTest(const ManipulatorContext& ctx,
                              const EditorViewport& viewport,
                              ImVec2 screenPos) const override;
    [[nodiscard]] std::unique_ptr<IInteraction> BeginDrag(
        int part, const ManipulatorContext& ctx, const EditorViewport& viewport,
        ImVec2 screenPos, ModifierFlags modifiers) const override;
};
