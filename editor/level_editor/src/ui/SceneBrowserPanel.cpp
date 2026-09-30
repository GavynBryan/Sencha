#include "SceneBrowserPanel.h"

#include "ui/chrome/ChromeControls.h"
#include "ui/chrome/ChromeTile.h"

#include "ui/ScopedPanel.h"
#include "ui/chrome/ChromeDecor.h"
#include "ui/TextFilterMatch.h"
#include "fonts/IconsFontAwesome6.h"

#include "commands/CommandStack.h"
#include "document/commands/SceneInstanceCommands.h"
#include "document/EditorDocument.h"
#include "document/WorldDocument.h"
#include "render/SceneThumbnailCache.h"
#include "scene_source/SceneSourcePaths.h"
#include "ui/EditorUiStyle.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <memory>

namespace
{
} // namespace

SceneBrowserPanel::SceneBrowserPanel(WorldDocument& world, SelectionService& selection,
                                     CommandStack& commands,
                                     std::vector<std::filesystem::path> contentRoots,
                      std::function<SceneThumbnailCache*()> thumbnails)
    : WorldDoc(world)
    , Selection(selection)
    , Commands(commands)
    , ContentRoots(std::move(contentRoots))
    , Thumbnails(std::move(thumbnails))
{
}

void SceneBrowserPanel::Rescan()
{
    Entries.clear();
    for (const std::filesystem::path& root : ContentRoots)
    {
        std::error_code ec;
        if (!std::filesystem::is_directory(root, ec))
            continue;
        for (std::filesystem::recursive_directory_iterator it(root, ec), end;
             it != end && !ec; it.increment(ec))
        {
            if (!it->is_regular_file(ec) || it->path().extension() != ".sscene")
                continue;
            std::error_code relEc;
            const std::filesystem::path relative =
                std::filesystem::relative(it->path(), root, relEc);
            if (relEc)
                continue;
            std::string assetPath = "asset://" + relative.generic_string();
            std::string label(SceneSourceStem(assetPath));
            Entries.push_back(Entry{ .AssetPath = std::move(assetPath),
                                     .Label = std::move(label) });
        }
    }
    std::sort(Entries.begin(), Entries.end(),
              [](const Entry& a, const Entry& b) { return a.AssetPath < b.AssetPath; });
    Scanned = true;
}

void SceneBrowserPanel::OnDraw()
{
    ScopedPanel panel(GetTitle(), &Visible, PanelStyle::Standard);
    if (!panel.IsOpen())
        return;
    if (!Scanned)
        Rescan();

    if (EditorChrome::ToolButton(ICON_FA_ARROWS_ROTATE "  Refresh", IconId::Refresh, ICON_FA_ARROWS_ROTATE "  Refresh", false, ImGui::GetFrameHeight()))
    {
        Rescan();
        if (SceneThumbnailCache* cache = Thumbnails ? Thumbnails() : nullptr)
            cache->Clear();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##filter", ICON_FA_MAGNIFYING_GLASS "  filter",
                             FilterText, sizeof(FilterText));
    ImGui::Separator();

    // A scene must not be placed into itself; nesting deeper cycles is the
    // resolver's refusal, but the direct case deserves a disabled cell here.
    const std::string_view focusPath = WorldDoc.FocusDocument().GetDisplayName();
    SceneThumbnailCache* thumbnails = Thumbnails ? Thumbnails() : nullptr;

    constexpr float kCell = 96.0f;
    const float rowWidth = ImGui::GetContentRegionAvail().x;
    int column = 0;
    const int columns = std::max(1, static_cast<int>(rowWidth / (kCell + 8.0f)));

    for (const Entry& entry : Entries)
    {
        if (!TextFilterMatch(entry.AssetPath, FilterText))
            continue;

        ImGui::PushID(entry.AssetPath.c_str());
        if (column > 0)
            ImGui::SameLine();

        const bool isSelf = !focusPath.empty()
            && focusPath.ends_with(entry.AssetPath.substr(sizeof("asset://") - 1));
        const EditorChrome::TileResult tile = EditorChrome::Tile({
            .Image = thumbnails != nullptr ? thumbnails->Thumbnail(entry.AssetPath) : 0,
            .Size = kCell, .Label = entry.Label, .Badge = IconId::Box, .Selected = isSelf, .Disabled = isSelf });
        ImGui::BeginDisabled(isSelf);
        if (ImGui::BeginDragDropSource())
        {
            ImGui::SetDragDropPayload(kDragPayloadType, entry.AssetPath.c_str(),
                                      entry.AssetPath.size() + 1);
            ImGui::TextUnformatted(entry.Label.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginPopupContextItem("##scene_ctx"))
        {
            if (ImGui::MenuItem(ICON_FA_PLUS "  Place at Origin"))
            {
                auto command = std::make_unique<PlaceSceneInstanceCommand>(
                    entry.AssetPath, Transform3f::Identity(),
                    WorldDoc.FocusDocument(), Selection);
                Commands.Execute(std::move(command));
            }
            ImGui::EndPopup();
        }
        ImGui::EndDisabled();
        const bool hovered = tile.Hovered;
        if (hovered)
            ImGui::SetTooltip(isSelf ? "%s (open scene: cannot place into itself)"
                                     : "%s",
                              entry.AssetPath.c_str());

        ImGui::PopID();

        column = (column + 1) % columns;
    }
    if (Entries.empty())
    {
        ImGui::TextDisabled("no .sscene sources under the content roots");
        EditorChrome::EmptyRegionLabel(EditorChrome::DecorSlot::SceneBrowserEmpty);
    }
}
