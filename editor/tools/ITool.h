#pragma once

#include <imgui.h>

#include <string_view>

struct EditorViewport;
struct ToolContext;

struct ITool
{
    virtual std::string_view GetId() const = 0;
    virtual std::string_view GetDisplayName() const = 0;
    virtual bool OnViewportClick(ToolContext& ctx, EditorViewport& viewport, ImVec2 point) = 0;
    virtual ~ITool() = default;
};
