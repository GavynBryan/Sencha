#pragma once

#include "IEditorPanel.h"

#include "../tools/ToolRegistry.h"
#include "../viewport/FourWayViewportLayout.h"

class ViewportPanel : public IEditorPanel
{
public:
    ViewportPanel(FourWayViewportLayout& layout, ToolRegistry& tools);

    std::string_view GetTitle() const override;
    bool IsVisible() const override;
    void OnDraw() override;

private:
    void DrawViewport(EditorViewport& viewport,
                      const char* id,
                      const char* label,
                      int index,
                      ImVec2 size);

    FourWayViewportLayout& Layout;
    ToolRegistry& Tools;
    bool Visible = true;
};
