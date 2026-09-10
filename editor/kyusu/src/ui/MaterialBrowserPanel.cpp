#include "MaterialBrowserPanel.h"

#include "ui/chrome/ChromeControls.h"
#include "ui/chrome/ChromeTile.h"

#include "ui/EditorUiStyle.h"
#include "ui/MaterialThumbnailCache.h"
#include "ui/ScopedPanel.h"
#include "ui/chrome/ChromeDecor.h"
#include "ui/TextFilterMatch.h"

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
    const EditorChrome::TileResult tile = EditorChrome::Tile({
        .Image = Thumbnails.Thumbnail(material.Path), .Size = cellSize,
        .Label = LeafName(material.DisplayName), .Selected = ActiveMaterial.Active.Path == material.Path });
    if (tile.Clicked)
        ActiveMaterial.Active = AssetRef{ AssetType::Material, material.Path };
    const bool hovered = tile.Hovered;
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

    ImGui::PopID();
}

void MaterialBrowserPanel::OnDraw()
{
    ScopedPanel panel(GetTitle(), &Visible, PanelStyle::Standard);
    if (!panel.IsOpen())
        return;

    if (EditorChrome::Button("Rescan", "Rescan", {}, EditorChrome::ButtonTone::Normal))
    {
        Materials.Rescan(Materials.Roots());
        // A rescan may follow a save that re-pointed a material's textures;
        // resolved base colors are stale until re-parsed.
        Thumbnails.Clear();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
    ImGui::InputTextWithHint("##filter", "Filter", Filter, sizeof(Filter));
    ImGui::SameLine();
    ImGui::TextDisabled("%zu found, %zu resident",
                        Materials.Materials().size(), Thumbnails.ResidentCount());

    std::vector<const MaterialAsset*> filtered;
    for (const MaterialAsset& material : Materials.Materials())
        if (TextFilterMatch(material.DisplayName, Filter))
            filtered.push_back(&material);

    if (filtered.empty())
    {
        ImGui::TextDisabled(Materials.Materials().empty()
                                ? "No .smat materials found. Save the level next to a materials/ folder, then Rescan."
                                : "No materials match the filter.");
        EditorChrome::EmptyRegionLabel(EditorChrome::DecorSlot::MaterialBrowserEmpty);
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
