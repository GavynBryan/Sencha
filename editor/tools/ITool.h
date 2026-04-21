#pragma once

#include "../input/InputEvent.h"

#include <imgui.h>

#include <string_view>

struct EditorViewport;
struct ToolContext;

struct ITool
{
    virtual std::string_view GetId() const = 0;
    virtual std::string_view GetDisplayName() const = 0;

    virtual void OnActivate(ToolContext& ctx) {}
    virtual void OnDeactivate(ToolContext& ctx) {}

    virtual InputConsumed OnPointerDown(ToolContext& ctx, EditorViewport& viewport, ImVec2 point) { return InputConsumed::No; }
    virtual InputConsumed OnPointerMove(ToolContext& ctx, EditorViewport& viewport, ImVec2 point, ImVec2 delta) { return InputConsumed::No; }
    virtual InputConsumed OnPointerUp(ToolContext& ctx, EditorViewport& viewport, ImVec2 point) { return InputConsumed::No; }
    virtual InputConsumed OnKeyDown(ToolContext& ctx, const KeyDownEvent& event) { return InputConsumed::No; }

    virtual ~ITool() = default;
};
