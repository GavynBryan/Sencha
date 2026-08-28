#include "SceneBrowserPanel.h"

#include "ui/ScopedPanel.h"
#include "fonts/IconsFontAwesome6.h"

#include "commands/CommandStack.h"
#include "document/commands/SceneInstanceCommands.h"
#include "document/EditorDocument.h"
#include "document/WorldDocument.h"
#include "render/SceneThumbnailCache.h"
#include "ui/EditorUiStyle.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <memory>

namespace
{
    [[nodiscard]] bool ContainsCaseInsensitive(std::string_view haystack,
                                               std::string_view needle)
    {
        if (needle.empty())
            return true;
        const auto it = std::search(
            haystack.begin(), haystack.end(), needle.begin(), needle.end(),
            [](char a, char b)
            {
                return std::tolower(static_cast<unsigned char>(a))
                    == std::tolower(static_cast<unsigned char>(b));
            });
        return it != haystack.end();
    }
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
            Entries.push_back(Entry{
                .AssetPath = "asset://" + relative.generic_string(),
                .Label = it->path().stem().string(),
            });
        }
    }
    std::sort(Entries.begin(), Entries.end(),
              [](const Entry& a, const Entry& b) { return a.AssetPath < b.AssetPath; });
    Scanned = true;
}

void SceneBrowserPanel::OnDraw()
{
    ScopedPanel panel(GetTitle(), &Visible);
    if (!panel.IsOpen())
        return;
    if (!Scanned)
        Rescan();

    if (ImGui::Button(ICON_FA_ARROWS_ROTATE "  Refresh"))
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
        if (!ContainsCaseInsensitive(entry.AssetPath, FilterText))
            continue;

        ImGui::PushID(entry.AssetPath.c_str());
        if (column > 0)
            ImGui::SameLine();

        const bool isSelf = !focusPath.empty()
            && focusPath.ends_with(entry.AssetPath.substr(sizeof("asset://") - 1));
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float labelHeight = ImGui::GetFontSize() + 4.0f;
        ImGui::BeginDisabled(isSelf);
        // One fixed-size item covers image and label, so the cell's layout
        // footprint never varies with the name -- the label renders through
        // the draw list, clipped, without advancing the cursor.
        ImGui::InvisibleButton("##cell", ImVec2(kCell, kCell + labelHeight));
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
        const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
        if (hovered)
            ImGui::SetTooltip(isSelf ? "%s (open scene: cannot place into itself)"
                                     : "%s",
                              entry.AssetPath.c_str());

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 end(pos.x + kCell, pos.y + kCell);
        const ImTextureID preview =
            thumbnails != nullptr ? thumbnails->Thumbnail(entry.AssetPath) : ImTextureID{};
        if (preview)
            drawList->AddImage(preview, pos, end);
        else
            drawList->AddRectFilled(pos, end, ImGui::GetColorU32(ImGuiCol_FrameBg));
        drawList->AddRect(pos, end,
                          ImGui::GetColorU32(hovered ? EditorUi::AccentHover
                                                     : EditorUi::Border));
        // The scene marker: the box in the lower-left corner, shadowed so it
        // reads over any preview.
        const ImVec2 badge(pos.x + 5.0f, end.y - ImGui::GetFontSize() - 4.0f);
        drawList->AddText(ImVec2(badge.x + 1.0f, badge.y + 1.0f),
                          ImGui::GetColorU32(EditorUi::AccentDim), ICON_FA_BOX_OPEN);
        drawList->AddText(badge, ImGui::GetColorU32(EditorUi::Accent),
                          ICON_FA_BOX_OPEN);

        // One clipped label line under the image, drawn without layout.
        const ImVec4 labelClip(pos.x, end.y, end.x, end.y + labelHeight);
        drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                          ImVec2(pos.x, end.y + 2.0f),
                          ImGui::GetColorU32(ImGuiCol_Text),
                          entry.Label.c_str(), nullptr, 0.0f, &labelClip);
        ImGui::PopID();

        column = (column + 1) % columns;
    }
    if (Entries.empty())
        ImGui::TextDisabled("no .sscene sources under the content roots");
}
