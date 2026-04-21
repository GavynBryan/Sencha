#pragma once

#include "IEditorPanel.h"

#include "../viewport/FourWayViewportLayout.h"

class ViewportPanel : public IEditorPanel
{
public:
    explicit ViewportPanel(FourWayViewportLayout& layout);

    std::string_view GetTitle() const override;
    bool IsVisible() const override;
    void OnDraw() override;

private:
    void DrawViewport(EditorViewport& viewport,
                      const char* id,
                      const char* label,
                      ImVec2 size);

    FourWayViewportLayout& Layout;
    bool Visible = true;
};
