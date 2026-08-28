#include "SceneBrowserPanel.h"

#include "ui/ScopedPanel.h"
#include "fonts/IconsFontAwesome6.h"

#include "commands/CommandStack.h"
#include "document/commands/SceneInstanceCommands.h"
#include "document/EditorDocument.h"
#include "document/WorldDocument.h"

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
                                     std::vector<std::filesystem::path> contentRoots)
    : WorldDoc(world)
    , Selection(selection)
    , Commands(commands)
    , ContentRoots(std::move(contentRoots))
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
        Rescan();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##filter", ICON_FA_MAGNIFYING_GLASS "  filter",
                             FilterText, sizeof(FilterText));
    ImGui::Separator();

    // A scene must not be placed into itself; nesting deeper cycles is the
    // resolver's refusal, but the direct case deserves a disabled row here.
    const std::string_view focusPath = WorldDoc.FocusDocument().GetDisplayName();

    for (const Entry& entry : Entries)
    {
        if (!ContainsCaseInsensitive(entry.AssetPath, FilterText))
            continue;

        ImGui::PushID(entry.AssetPath.c_str());
        const bool isSelf = !focusPath.empty()
            && focusPath.ends_with(entry.AssetPath.substr(sizeof("asset://") - 1));
        ImGui::BeginDisabled(isSelf);
        ImGui::Selectable((std::string(ICON_FA_BOX_OPEN "  ") + entry.Label).c_str());
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
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(isSelf ? "%s (open scene: cannot place into itself)"
                                     : "%s",
                              entry.AssetPath.c_str());
        ImGui::PopID();
    }
    if (Entries.empty())
        ImGui::TextDisabled("no .sscene sources under the content roots");
}
