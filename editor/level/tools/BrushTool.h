#pragma once

#include "../../tools/ITool.h"

class BrushTool : public ITool
{
public:
    std::string_view GetId() const override;
    std::string_view GetDisplayName() const override;
    InputConsumed OnPointerDown(ToolContext& ctx, EditorViewport& viewport, ImVec2 point) override;
};
