#pragma once

#include "../../tools/ITool.h"

class SelectTool : public ITool
{
public:
    std::string_view GetId() const override;
    std::string_view GetDisplayName() const override;
    bool OnViewportClick(ToolContext& ctx, EditorViewport& viewport, ImVec2 point) override;
};
