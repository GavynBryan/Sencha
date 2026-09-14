#pragma once

#include "ui/IEditorPanel.h"
#include <functional>

class ToolRegistry;

// Registered tools in a wrapping bay. The resolver follows workspace resets;
// switching tools retains ToolRegistry's interaction cancellation semantics.
class ToolPalettePanel : public IEditorPanel
{
public:
    explicit ToolPalettePanel(std::function<ToolRegistry*()> tools);
    std::string_view GetTitle() const override { return "TOOLS"; }
    DockSlot GetDockSlot() const override { return DockSlot::LeftEdge; }
    PanelPersistence GetPersistence() const override { return { "tools", PanelVisibilityPolicy::Remembered }; }
    void OnDraw() override;

private:
    std::function<ToolRegistry*()> ToolsResolver;
};
