#pragma once

#include <imgui.h>

struct ToolContext;
struct EditorViewport;

struct IInteraction
{
    virtual void OnPointerMove(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos, ImVec2 delta) = 0;
    virtual void OnPointerUp(ToolContext& ctx, EditorViewport& viewport, ImVec2 pos) = 0;
    virtual void OnCancel(ToolContext& ctx) = 0;
    virtual ~IInteraction() = default;
};
