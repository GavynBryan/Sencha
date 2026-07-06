#include "ActiveMaterialPanel.h"

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

void ActiveMaterialPanel::PaintMaterialSquare(const AssetRef& material, ImVec2 pos, float size,
                                              bool highlight)
{
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 end(pos.x + size, pos.y + size);
    ImTextureID tex = 0;
    if (material.IsValid())
        tex = Thumbnails.Thumbnail(material.Path);
    if (tex != 0)
        drawList->AddImage(tex, pos, end);
    else
        drawList->AddRectFilled(pos, end, ImGui::GetColorU32(ImGuiCol_FrameBg));
    drawList->AddRect(pos, end,
                      ImGui::GetColorU32(highlight ? EditorUi::Accent : EditorUi::Border),
                      0.0f, 0, highlight ? 2.0f : 1.0f);
}

void ActiveMaterialPanel::OnDraw()
{
    ScopedPanel panel(GetTitle(), &Visible);
    if (!panel.IsOpen())
        return;

    const float avail = ImGui::GetContentRegionAvail().x;

    // The preview: the material Shift+T / Apply will put on faces. Capped so the
    // Browse + slots row stays on screen in a narrow column without scrolling.
    const float previewSize = std::clamp(avail, 48.0f, 148.0f);
    const ImVec2 previewPos = ImGui::GetCursorScreenPos();
    PaintMaterialSquare(ActiveMaterial.Active, previewPos, previewSize, ActiveMaterial.Active.IsValid());
    ImGui::Dummy(ImVec2(previewSize, previewSize));

    if (ActiveMaterial.Active.IsValid())
        ImGui::TextWrapped("%s", MaterialDisplayName(ActiveMaterial.Active.Path).c_str());
    else
        ImGui::TextDisabled("No active material.\nPick one in Materials or Shift+RClick a face.");

    // Browse and the three stash slots share one row. Swap-on-click: a filled
    // slot swaps with the active material (stash + recall in one gesture); an
    // empty slot stores a copy (the active slot is never emptied by a click).
    if (ImGui::Button("Browse") && Browse)
        Browse();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Show the Materials browser.");

    const float slotSize = ImGui::GetFrameHeight() * 1.8f;
    for (int i = 0; i < static_cast<int>(ActiveMaterial.Slots.size()); ++i)
    {
        AssetRef& slot = ActiveMaterial.Slots[static_cast<std::size_t>(i)];
        ImGui::PushID(i);
        ImGui::SameLine();

        const ImVec2 pos = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##slot", ImVec2(slotSize, slotSize)))
        {
            if (slot.IsValid())
                std::swap(slot, ActiveMaterial.Active);
            else
                slot = ActiveMaterial.Active;
        }
        const bool hovered = ImGui::IsItemHovered();
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

        PaintMaterialSquare(slot, pos, slotSize, hovered && slot.IsValid());
        ImGui::PopID();
    }
}
