#pragma once

#include "interaction/IInteraction.h"

// A press on a pending placement, dragged: it turns to face the pointer,
// snapping by `step` radians while snapping is on. The release places it and
// keeps its yaw for the next placement; a cancel turns it back.
class PlaceTurnInteraction final : public IInteraction
{
public:
    PlaceTurnInteraction(float step, float& restingYaw);

    void OnPointerMove(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) override;
    void OnPointerUp(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) override;
    void OnCancel(ToolContext& ctx) override;

private:
    float Step;
    float& RestingYaw;
};
