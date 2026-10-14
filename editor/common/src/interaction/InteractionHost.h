#pragma once

#include "IInteraction.h"

#include "input/InputEvent.h"

#include <math/Vec.h>
#include <memory>

struct ToolContext;
struct EditorViewport;

class InteractionHost
{
public:
    // Begins an interaction, cancelling (reverting) any already-active one first
    // so a drag can never be silently abandoned mid-flight. (W4.)
    void Begin(ToolContext& ctx, std::unique_ptr<IInteraction> interaction);
    void Cancel(ToolContext& ctx);
    bool IsActive() const;

    InputConsumed OnPointerMove(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer);
    InputConsumed OnPointerUp(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer);
    void OnPointerLeftViews(ToolContext& ctx);

private:
    std::unique_ptr<IInteraction> Active;
};
