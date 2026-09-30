#pragma once

#include "MaterialDocumentSet.h"

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
                         MaterialDocumentSet& tabs,
                         std::function<void(std::size_t)> closeTab);

    [[nodiscard]] std::string_view GetTitle() const override { return "Preview"; }
    [[nodiscard]] DockSlot GetDockSlot() const override { return DockSlot::Center; }
    [[nodiscard]] PanelPersistence GetPersistence() const override { return { "preview", PanelVisibilityPolicy::SessionOnly }; }
    void OnDraw() override;

private:
    void DrawTabBar();
    void DrawPreviewImage();

    MaterialPreviewRenderFeature& Preview;
    MaterialDocumentSet& Tabs;
    std::function<void(std::size_t)> CloseTab;
    // The active tab the bar last showed.
    std::size_t ShownActive = static_cast<std::size_t>(-1);
};
