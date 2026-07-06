#include "MaterialBrowserPanel.h"

#include "ui/EditorUiStyle.h"
#include "ui/MaterialThumbnailCache.h"
#include "ui/ScopedPanel.h"

#include "project/MaterialLibrary.h"
#include "meshedit/ActiveMaterialState.h"

#include <core/console/ConsoleRegistry.h>
#include <core/console/ConsoleTypes.h>

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <utility>
#include <variant>
#include <vector>

namespace
{
    bool MatchesFilter(const std::string& name, const char* filter)
    {
        if (filter[0] == '\0')
            return true;
        const auto toLower = [](std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        };
        return toLower(name).find(toLower(filter)) != std::string::npos;
    }

    // "materials/dev/gray" -> "gray": the cell label; the tooltip carries the
    // full path.
    std::string_view LeafName(std::string_view displayName)
    {
        const std::size_t slash = displayName.find_last_of('/');
        return slash == std::string_view::npos ? displayName : displayName.substr(slash + 1);
    }
}

MaterialBrowserPanel::MaterialBrowserPanel(MaterialLibrary& materials,
                                           MaterialThumbnailCache& thumbnails,
                                           ActiveMaterialState& activeMaterial,
                                           const ConsoleRegistry& console,
                                           std::function<void()> applyToSelection)
    : Materials(materials)
    , Thumbnails(thumbnails)
    , ActiveMaterial(activeMaterial)
    , Console(console)
    , ApplyToSelection(std::move(applyToSelection))
{
}

void MaterialBrowserPanel::Reveal()
{
    SetVisible(true);
    ImGui::SetWindowFocus(GetTitle().data());
}

void MaterialBrowserPanel::DrawCell(const MaterialAsset& material, float cellSize)
{
    ImGui::PushID(material.Path.c_str());
    ImGui::BeginGroup();

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("##cell", ImVec2(cellSize, cellSize)))
        ActiveMaterial.Active = AssetRef{ AssetType::Material, material.Path };
    const bool hovered = ImGui::IsItemHovered();
    if (hovered)
        ImGui::SetTooltip("%s", material.Path.c_str());
    if (ImGui::BeginPopupContextItem("##cellmenu"))
    {
        if (ImGui::MenuItem("Set active"))
            ActiveMaterial.Active = AssetRef{ AssetType::Material, material.Path };
        if (ApplyToSelection && ImGui::MenuItem("Apply to selected faces"))
        {
            ActiveMaterial.Active = AssetRef{ AssetType::Material, material.Path };
            ApplyToSelection();
        }
        ImGui::EndPopup();
    }

    const bool isActive = ActiveMaterial.Active.Path == material.Path;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 end(pos.x + cellSize, pos.y + cellSize);
    if (const ImTextureID tex = Thumbnails.Thumbnail(material.Path))
        drawList->AddImage(tex, pos, end);
    else
        drawList->AddRectFilled(pos, end, ImGui::GetColorU32(ImGuiCol_FrameBg));
    const ImU32 border = isActive  ? ImGui::GetColorU32(EditorUi::Accent)
                         : hovered ? ImGui::GetColorU32(EditorUi::AccentHover)
                                   : ImGui::GetColorU32(EditorUi::Border);
    drawList->AddRect(pos, end, border, 0.0f, 0, isActive ? 2.0f : 1.0f);

    // One clipped label line under the image; long names read via the tooltip.
    const std::string_view leaf = LeafName(material.DisplayName);
    const float lineHeight = ImGui::GetTextLineHeight();
    const ImVec2 textPos = ImGui::GetCursorScreenPos();
    drawList->PushClipRect(textPos, ImVec2(textPos.x + cellSize, textPos.y + lineHeight), true);
    drawList->AddText(textPos,
                      ImGui::GetColorU32(isActive || hovered ? EditorUi::TextPrimary : EditorUi::TextDim),
                      leaf.data(), leaf.data() + leaf.size());
    drawList->PopClipRect();
    ImGui::Dummy(ImVec2(cellSize, lineHeight));

    ImGui::EndGroup();
    ImGui::PopID();
}

void MaterialBrowserPanel::OnDraw()
{
    ScopedPanel panel(GetTitle(), &Visible);
    if (!panel.IsOpen())
        return;

    if (ImGui::Button("Rescan"))
    {
        Materials.Rescan(Materials.Roots());
        // A rescan may follow a save that re-pointed a material's textures;
        // resolved base colors are stale until re-parsed.
        Thumbnails.Clear();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputTextWithHint("##filter", "Filter", Filter, sizeof(Filter));
    ImGui::SameLine();
    ImGui::TextDisabled("%zu found, %zu resident",
                        Materials.Materials().size(), Thumbnails.ResidentCount());

    std::vector<const MaterialAsset*> filtered;
    for (const MaterialAsset& material : Materials.Materials())
        if (MatchesFilter(material.DisplayName, Filter))
            filtered.push_back(&material);

    if (filtered.empty())
    {
        ImGui::TextDisabled(Materials.Materials().empty()
                                ? "No .smat materials found. Save the level next to a materials/ folder, then Rescan."
                                : "No materials match the filter.");
        return;
    }

    const auto readInt = [&](const char* name, std::int64_t fallback)
    {
        if (const CVarMetadata* cvar = Console.FindCVar(name);
            cvar != nullptr && std::holds_alternative<std::int64_t>(cvar->CurrentValue))
            return std::get<std::int64_t>(cvar->CurrentValue);
        return fallback;
    };
    const auto readDouble = [&](const char* name, double fallback)
    {
        if (const CVarMetadata* cvar = Console.FindCVar(name);
            cvar != nullptr && std::holds_alternative<double>(cvar->CurrentValue))
            return std::get<double>(cvar->CurrentValue);
        return fallback;
    };
    const float cellSize = std::max(32.0f, static_cast<float>(readDouble("editor.materials.thumbnail_size", 96.0)));
    const auto budget = static_cast<std::size_t>(std::max<std::int64_t>(
        1, readInt("editor.materials.thumbnail_budget", 128)));

    ImGui::BeginChild("##materialgrid");
    const ImGuiStyle& style = ImGui::GetStyle();
    const float cellStride = cellSize + style.ItemSpacing.x;
    const int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / cellStride));
    const int rows = (static_cast<int>(filtered.size()) + columns - 1) / columns;
    const float rowHeight = cellSize + ImGui::GetTextLineHeight() + style.ItemSpacing.y;

    // The clipper is the soft pagination: only visible rows request thumbnails,
    // so off-screen cells never touch the GPU.
    ImGuiListClipper clipper;
    clipper.Begin(rows, rowHeight);
    while (clipper.Step())
    {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
        {
            for (int column = 0; column < columns; ++column)
            {
                const std::size_t index = static_cast<std::size_t>(row) * columns + column;
                if (index >= filtered.size())
                    break;
                if (column > 0)
                    ImGui::SameLine();
                DrawCell(*filtered[index], cellSize);
            }
        }
    }
    ImGui::EndChild();

    Thumbnails.TrimToBudget(budget);
}
