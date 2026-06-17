#pragma once

#include "../../tools/ITool.h"

// The selection tool. A press-drag-release gesture: a short press is a click
// (single pick under the cursor); dragging past a threshold is a marquee box
// select. Both resolve on release, per the active element mode, with Shift = add
// and Ctrl = remove. A manipulator drag, when hit, runs before the tool (the
// session consumes the press first), so this only fires on empty/selection input.
class SelectTool : public ITool
{
public:
    std::string_view GetId() const override;
    std::string_view GetDisplayName() const override;
    InputConsumed OnPointerDown(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) override;
    InputConsumed OnPointerMove(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) override;
    InputConsumed OnPointerUp(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) override;

private:
    bool Pressed = false;
    ModifierFlags PressModifiers = {};
};
