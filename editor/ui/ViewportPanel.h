#pragma once

#include "IEditorPanel.h"

#include "../viewport/ViewportLayout.h"

class ViewportPanel : public IEditorPanel
{
public:
    explicit ViewportPanel(ViewportLayout& layout);

    std::string_view GetTitle() const override;
    bool IsVisible() const override;
    void OnDraw() override;

private:
    void DrawNode(const LayoutNode& node, ImVec2 size);
    void DrawViewport(EditorViewport& viewport, ImVec2 size);
    void DrawOrientationSelector(EditorViewport& viewport);

    ViewportLayout& Layout;
    bool Visible = true;
};
