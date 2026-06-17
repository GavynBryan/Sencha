#pragma once

#include "../input/InputEvent.h"

#include <imgui.h>

struct ToolContext;
struct EditorViewport;

struct IEditSession
{
    virtual InputConsumed OnPointerDown(ToolContext& ctx, EditorViewport& viewport, const PointerEvent& pointer) = 0;
    virtual ~IEditSession() = default;
};
