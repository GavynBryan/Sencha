#pragma once

#include "ui/IEditorPanel.h"

#include <core/assets/AssetRef.h>

#include <imgui.h>

#include <functional>

class MaterialThumbnailCache;
class ActiveResources;

// The active-material readout in the left column, under the Mesh Edit panel:
// a large preview of the material the texturing verbs will apply, a Browse
// jump to the Materials panel, and three stash slots. Clicking a filled slot
// swaps it with the active material (one gesture stashes and recalls; nothing
// is lost); clicking an empty slot stores a copy of the active material.
class ActiveMaterialPanel : public IEditorPanel
{
public:
    ActiveMaterialPanel(ActiveResources& resources,
                        MaterialThumbnailCache& thumbnails,
                        std::function<void()> browse);

    std::string_view GetTitle() const override { return "ACTIVE MATERIAL"; }
    void OnDraw() override;
    DockSlot GetDockSlot() const override { return DockSlot::Left; }
    PanelPersistence GetPersistence() const override { return { "active_material", PanelVisibilityPolicy::Remembered }; }

private:
    ActiveResources& Resources;
    MaterialThumbnailCache& Thumbnails;
    std::function<void()> Browse;
};
