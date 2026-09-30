#include "ActiveMaterialPanel.h"

#include "ui/chrome/ChromeControls.h"
#include "ui/chrome/ChromeTile.h"

#include "ui/EditorUiStyle.h"
#include "ui/MaterialThumbnailCache.h"
#include "ui/ScopedPanel.h"

#include "project/MaterialLibrary.h"
#include "resources/ActiveResources.h"

#include <imgui.h>

#include <algorithm>
#include <utility>

ActiveMaterialPanel::ActiveMaterialPanel(ActiveResources& resources,
                                         MaterialThumbnailCache& thumbnails,
                                         std::function<void()> browse)
    : Resources(resources)
    , Thumbnails(thumbnails)
    , Browse(std::move(browse))
{
}

void ActiveMaterialPanel::OnDraw()
{
    ScopedPanel panel(GetWindowName(), &Visible, PanelStyle::Compact);
    if (!panel.IsOpen())
        return;

    const float avail = ImGui::GetContentRegionAvail().x;

    // The preview: the material Shift+T / Apply will put on faces. Capped so the
    // Browse + slots row stays on screen in a narrow column without scrolling.
    const float previewSize = std::clamp(avail, 48.0f, 148.0f);
    EditorChrome::Tile({ .Image = Resources.Active(AssetType::Material).IsValid() ? Thumbnails.Thumbnail(Resources.Active(AssetType::Material).Path) : 0,
                         .Size = previewSize, .Selected = Resources.Active(AssetType::Material).IsValid(), .Interactive = false });

    if (Resources.Active(AssetType::Material).IsValid())
        ImGui::TextWrapped("%s", MaterialDisplayName(Resources.Active(AssetType::Material).Path).c_str());
    else
        ImGui::TextDisabled("No active material.\nPick one in Materials or Shift+RClick a face.");

    // Browse and the three stash slots share one row. Swap-on-click: a filled
    // slot swaps with the active material (stash + recall in one gesture); an
    // empty slot stores a copy (the active slot is never emptied by a click).
    if (EditorChrome::ToolButton("Browse", IconId::Folder, "Browse", false, ImGui::GetFrameHeight()) && Browse)
        Browse();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Show the Materials browser.");

    const float slotSize = ImGui::GetFrameHeight() * 1.8f;
    const ActiveResources::StashSlots& stash = Resources.Stash(AssetType::Material);
    for (std::size_t i = 0; i < stash.size(); ++i)
    {
        const AssetRef& slot = stash[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::SameLine();

        const EditorChrome::TileResult tile = EditorChrome::Tile({
            .Image = slot.IsValid() ? Thumbnails.Thumbnail(slot.Path) : 0, .Size = slotSize });
        if (tile.Clicked)
            Resources.UseStashSlot(AssetType::Material, i);
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
            if (ImGui::MenuItem("Store active here", nullptr, false, Resources.Active(AssetType::Material).IsValid()))
                Resources.StoreInStash(AssetType::Material, i);
            if (ImGui::MenuItem("Clear", nullptr, false, slot.IsValid()))
                Resources.ClearStashSlot(AssetType::Material, i);
            ImGui::EndPopup();
        }

        ImGui::PopID();
    }
}
