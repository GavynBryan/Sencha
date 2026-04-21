#pragma once

#include "ITool.h"

#include <imgui.h>

#include <memory>
#include <string_view>
#include <vector>

struct EditorViewport;
struct ToolContext;

class ToolRegistry
{
public:
    explicit ToolRegistry(ToolContext& context);

    void Register(std::unique_ptr<ITool> tool);
    bool Activate(std::string_view id);
    bool Activate(std::size_t index);

    [[nodiscard]] ITool* GetActiveTool();
    [[nodiscard]] const ITool* GetActiveTool() const;
    [[nodiscard]] int GetActiveIndex() const;
    [[nodiscard]] const std::vector<std::unique_ptr<ITool>>& GetTools() const;

    bool HandleViewportClick(EditorViewport& viewport, ImVec2 point);

private:
    ToolContext& Context;
    std::vector<std::unique_ptr<ITool>> Tools;
    int ActiveIndex = -1;
};
