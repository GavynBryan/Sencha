#pragma once

#include "MaterialTabSet.h"

#include "ui/IEditorPanel.h"

#include <cstddef>
#include <functional>

class MaterialPreviewRenderFeature;

// The central surface: a tab per open material (dirty marker, close box) over
// the preview render feature's offscreen image. Orbit/zoom comes from ImGui
// mouse deltas over the image (no viewport input stack: one image, one
// camera). Closing routes through the composition root, which owns the tab's
// resident material handle.
class MaterialPreviewPanel final : public IEditorPanel
{
public:
    MaterialPreviewPanel(MaterialPreviewRenderFeature& preview,
                         MaterialTabSet& tabs,
                         std::function<void(std::size_t)> closeTab);

    [[nodiscard]] std::string_view GetTitle() const override { return "Preview"; }
    [[nodiscard]] DockSlot GetDockSlot() const override { return DockSlot::Center; }
    void OnDraw() override;

private:
    void DrawTabBar();
    void DrawPreviewImage();

    MaterialPreviewRenderFeature& Preview;
    MaterialTabSet& Tabs;
    std::function<void(std::size_t)> CloseTab;
};
