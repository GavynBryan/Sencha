#pragma once

#include "IManipulator.h"

// 3-axis translate manipulator. Hit-tests the X/Y/Z axes in screen space and
// produces an axis-constrained, grid-snapped drag; object mode moves the entity
// transform, element modes move mesh vertices — both through the ManipulationSink.
// Draws itself as 3 colored arrows. Stateless: the pivot is derived from context
// each call. First concrete IManipulator. (08-select-tool-v2.md)
class TranslateManipulator : public IManipulator
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
