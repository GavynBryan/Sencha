#pragma once

#include "../input/InputEvent.h"

#include <imgui.h>

struct ToolContext;
struct EditorViewport;

struct IEditSession
{
    virtual InputConsumed OnPointerDown(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos) = 0;
    virtual ~IEditSession() = default;
};
