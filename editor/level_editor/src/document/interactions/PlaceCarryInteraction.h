#pragma once

#include "document/placement/PendingPlacement.h"
#include "interaction/IInteraction.h"

// Something carried into the views with the button held: pending wherever the
// pointer points, nowhere while it is over no view. The release places it as
// shown; a cancel places nothing.
class PlaceCarryInteraction final : public IInteraction
{
public:
    PlaceCarryInteraction(PlacementSubject subject, float yaw);

    void OnPointerMove(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) override;
    void OnPointerUp(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) override;
    void OnCancel(ToolContext& ctx) override;
    void OnPointerLeftViews(ToolContext& ctx) override;

private:
    PlacementSubject Subject;
    float Yaw = 0.0f;
};
