#pragma once

#include "interaction/IInteraction.h"

// The rubber-band box-select drag. SelectTool::BeginDrag starts it once the press
// crosses the gesture deadzone; it updates the shared MarqueeState overlay during
// the drag and resolves the box pick (per the active element mode + the modifiers
// held at press) on release. Cancelled cleanly on Escape / focus loss like any
// interaction.
class MarqueeInteraction : public IInteraction
{
public:
    explicit MarqueeInteraction(ModifierFlags pressModifiers);

    void OnPointerMove(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) override;
    void OnPointerUp(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) override;
    void OnCancel(ToolContext& ctx) override;

private:
    ModifierFlags PressModifiers;
};
