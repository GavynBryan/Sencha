#pragma once

#include "IEditorPanel.h"

class ToolRegistry;

class ToolPalettePanel : public IEditorPanel
{
public:
    explicit ToolPalettePanel(ToolRegistry& tools);

    std::string_view GetTitle() const override;
    bool IsVisible() const override;
    void OnDraw() override;

private:
    ToolRegistry& Tools;
    bool Visible = true;
};
