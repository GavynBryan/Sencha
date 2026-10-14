#pragma once

#include "input/InputEvent.h"

struct ToolContext;
struct EditorViewport;

struct IInteraction
{
    virtual void OnPointerMove(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) = 0;
    virtual void OnPointerUp(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) = 0;
    virtual void OnCancel(ToolContext& ctx) = 0;
    // A gesture that follows the pointer from view to view is over none of
    // them; a pinned gesture never hears this.
    virtual void OnPointerLeftViews(ToolContext& /*ctx*/) {}
    virtual ~IInteraction() = default;
};
