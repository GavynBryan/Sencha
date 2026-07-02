#pragma once

#include "ui/IEditorPanel.h"

class MaterialPreviewRenderFeature;

// Shows the preview render feature's offscreen target and feeds it orbit/zoom
// input from ImGui mouse deltas over the image (no viewport input stack: one
// image, one camera).
class MaterialPreviewPanel final : public IEditorPanel
{
public:
    explicit MaterialPreviewPanel(MaterialPreviewRenderFeature& preview);

    [[nodiscard]] std::string_view GetTitle() const override { return "Preview"; }
    [[nodiscard]] DockSlot GetDockSlot() const override { return DockSlot::Center; }
    void OnDraw() override;

private:
    MaterialPreviewRenderFeature& Preview;
};
