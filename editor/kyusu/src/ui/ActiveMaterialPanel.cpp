#include "ActiveMaterialPanel.h"

#include "ui/chrome/ChromeControls.h"
#include "ui/chrome/ChromeTile.h"

#include "ui/EditorUiStyle.h"
#include "ui/MaterialThumbnailCache.h"
#include "ui/ScopedPanel.h"

#include "project/MaterialLibrary.h"
#include "meshedit/ActiveMaterialState.h"

#include <imgui.h>

#include <algorithm>
#include <utility>

ActiveMaterialPanel::ActiveMaterialPanel(ActiveMaterialState& activeMaterial,
                                         MaterialThumbnailCache& thumbnails,
                                         std::function<void()> browse)
    : ActiveMaterial(activeMaterial)
    , Thumbnails(thumbnails)
    , Browse(std::move(browse))
{
}

void ActiveMaterialPanel::OnDraw()
{
    ScopedPanel panel(GetTitle(), &Visible, PanelStyle::Compact);
    if (!panel.IsOpen())
        return;

    const float avail = ImGui::GetContentRegionAvail().x;

    // The preview: the material Shift+T / Apply will put on faces. Capped so the
    // Browse + slots row stays on screen in a narrow column without scrolling.
    const float previewSize = std::clamp(avail, 48.0f, 148.0f);
    EditorChrome::Tile({ .Image = ActiveMaterial.Active.IsValid() ? Thumbnails.Thumbnail(ActiveMaterial.Active.Path) : 0,
                         .Size = previewSize, .Selected = ActiveMaterial.Active.IsValid(), .Interactive = false });

    if (ActiveMaterial.Active.IsValid())
        ImGui::TextWrapped("%s", MaterialDisplayName(ActiveMaterial.Active.Path).c_str());
    else
        ImGui::TextDisabled("No active material.\nPick one in Materials or Shift+RClick a face.");

    // Browse and the three stash slots share one row. Swap-on-click: a filled
    // slot swaps with the active material (stash + recall in one gesture); an
    // empty slot stores a copy (the active slot is never emptied by a click).
    if (EditorChrome::Button("Browse", "Browse", {}, EditorChrome::ButtonTone::Normal) && Browse)
        Browse();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Show the Materials browser.");

    const float slotSize = ImGui::GetFrameHeight() * 1.8f;
    for (int i = 0; i < static_cast<int>(ActiveMaterial.Slots.size()); ++i)
    {
        AssetRef& slot = ActiveMaterial.Slots[static_cast<std::size_t>(i)];
        ImGui::PushID(i);
        ImGui::SameLine();

        const EditorChrome::TileResult tile = EditorChrome::Tile({
            .Image = slot.IsValid() ? Thumbnails.Thumbnail(slot.Path) : 0, .Size = slotSize });
        if (tile.Clicked)
        {
            if (slot.IsValid())
                std::swap(slot, ActiveMaterial.Active);
            else
                slot = ActiveMaterial.Active;
        }
        const bool hovered = tile.Hovered;
        if (hovered)
        {
            if (slot.IsValid())
                ImGui::SetTooltip("%s\nClick: swap with the active material.",
                                  MaterialDisplayName(slot.Path).c_str());
            else
                ImGui::SetTooltip("Empty slot.\nClick: store the active material here.");
        }
        if (ImGui::BeginPopupContextItem("##slotmenu"))
        {
            if (ImGui::MenuItem("Store active here", nullptr, false, ActiveMaterial.Active.IsValid()))
                slot = ActiveMaterial.Active;
            if (ImGui::MenuItem("Clear", nullptr, false, slot.IsValid()))
                slot = {};
            ImGui::EndPopup();
        }

        ImGui::PopID();
    }
}
