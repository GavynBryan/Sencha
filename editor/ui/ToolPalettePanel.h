#pragma once

#include "IEditorPanel.h"

class ToolRegistry;

class ToolPalettePanel : public IEditorPanel
{
public:
    explicit ToolPalettePanel(ToolRegistry& tools);

    std::string_view GetTitle() const override;
    void OnDraw() override;
    DockSlot GetDockSlot() const override { return DockSlot::Left; }

private:
    ToolRegistry& Tools;
};
