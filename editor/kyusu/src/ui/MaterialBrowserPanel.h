#pragma once

#include "ui/IEditorPanel.h"

#include <functional>
#include <string>

class ConsoleRegistry;
class MaterialLibrary;
class MaterialThumbnailCache;
struct ActiveMaterialState;
struct MaterialAsset;

// The material picking surface: a filterable thumbnail grid over the
// MaterialLibrary. Clicking a cell makes that material the active material.
// Only the grid rows on screen request thumbnails (ImGuiListClipper), and the
// cache is trimmed to the editor.materials.thumbnail_budget cvar after every
// draw, so an arbitrarily large library scrolls without holding every base
// color texture resident.
class MaterialBrowserPanel : public IEditorPanel
{
public:
    MaterialBrowserPanel(MaterialLibrary& materials,
                         MaterialThumbnailCache& thumbnails,
                         ActiveMaterialState& activeMaterial,
                         const ConsoleRegistry& console,
                         std::function<void()> applyToSelection);

    std::string_view GetTitle() const override { return "Materials"; }
    void OnDraw() override;
    DockSlot GetDockSlot() const override { return DockSlot::CenterBottom; }
    // Shares one tabbed node with the Console in the center-bottom strip.
    int GetDockTabGroup() const override { return 0; }

    // Shows the panel and raises its tab (the Browse jump from the Active
    // Material panel). Must run inside the ImGui frame.
    void Reveal();

private:
    void DrawCell(const MaterialAsset& material, float cellSize);

    MaterialLibrary& Materials;
    MaterialThumbnailCache& Thumbnails;
    ActiveMaterialState& ActiveMaterial;
    const ConsoleRegistry& Console;
    std::function<void()> ApplyToSelection;

    char Filter[128] = {};
};
