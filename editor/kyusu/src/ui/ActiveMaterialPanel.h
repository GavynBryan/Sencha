#pragma once

#include "ui/IEditorPanel.h"

#include <core/assets/AssetRef.h>

#include <imgui.h>

#include <functional>

class MaterialThumbnailCache;
struct ActiveMaterialState;

// The active-material readout in the left column, under the Mesh Edit panel:
// a large preview of the material the texturing verbs will apply, a Browse
// jump to the Materials panel, and three stash slots. Clicking a filled slot
// swaps it with the active material (one gesture stashes and recalls; nothing
// is lost); clicking an empty slot stores a copy of the active material.
class ActiveMaterialPanel : public IEditorPanel
{
public:
    ActiveMaterialPanel(ActiveMaterialState& activeMaterial,
                        MaterialThumbnailCache& thumbnails,
                        std::function<void()> browse);

    std::string_view GetTitle() const override { return "ACTIVE MATERIAL"; }
    void OnDraw() override;
    DockSlot GetDockSlot() const override { return DockSlot::Left; }

private:
    // Paints a square material preview at `pos` (thumbnail or placeholder
    // frame); layout space is the caller's job.
    // How a square's outline reads: the steel hairline, the cyan hover, or the
    // amber of the material currently applied by the tools.
    enum class Outline
    {
        Plain,
        Hovered,
        Selected,
    };
    void PaintMaterialSquare(const AssetRef& material, ImVec2 pos, float size, Outline outline);

    ActiveMaterialState& ActiveMaterial;
    MaterialThumbnailCache& Thumbnails;
    std::function<void()> Browse;
};
