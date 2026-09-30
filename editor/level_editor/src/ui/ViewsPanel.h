#pragma once

#include "ui/IEditorPanel.h"
#include "ui/chrome/PanelStyle.h"

#include "viewport/ViewportLayout.h"

#include <imgui.h>
#include <math/geometry/2d/Rect2d.h>

#include <functional>
#include <string_view>
#include <vector>

struct WorkspaceView;

// Where the Level's views show while the workspace still docks panels. The
// views and what is drawn over them are on the window's content surface, under
// the panels; this panel holds the dock's centre for them, transparent where
// they show, with the toolbar row above them. It goes when the dock does.
class ViewsPanel : public IEditorPanel
{
public:
    ViewsPanel(ViewportLayout& layout, WorkspaceView& view);

    std::string_view GetTitle() const override { return "VIEWS"; }
    // The central node has no tab bar and no View entry: nothing can hide it.
    PanelPersistence GetPersistence() const override { return { "viewport", PanelVisibilityPolicy::SessionOnly }; }
    void OnDraw() override;
    DockSlot GetDockSlot() const override { return DockSlot::Center; }

    // The area the views share, in window pixels, as last drawn; empty while
    // the panel is not on screen.
    [[nodiscard]] Rect2d ContentRect() const { return Content; }

    // True when the pointer is over the views with no panel above them, as of
    // the last drawn frame: input there belongs to the scene.
    [[nodiscard]] bool IsViewRegionHovered() const { return RegionHovered; }

    // A scene source dropped from the browser: the view it landed in, the
    // drop position in window pixels, and the asset:// path it carried.
    using SceneDropHandler = std::function<void(ViewportId, Vec2d, std::string_view)>;
    void SetSceneDropHandler(SceneDropHandler handler) { SceneDrop = std::move(handler); }

    // Rows of chrome reserved above the views, drawn by whoever owns what is
    // on them.
    struct ChromeRow
    {
        std::function<float()> Height;
        std::function<void(ImDrawList* dl, ImVec2 mn, ImVec2 mx)> Draw;
    };
    void SetHeaderRows(std::vector<ChromeRow> rows) { Rows = std::move(rows); }

private:
    void DrawChromeRow(const ChromeRow& row);

    ViewportLayout& Layout;
    WorkspaceView& View;
    Rect2d Content{};
    bool RegionHovered = false;
    SceneDropHandler SceneDrop;
    std::vector<ChromeRow> Rows;
};
