#include "MaterialBrowserPanel.h"

#include "MaterialEditSession.h"

#include "project/MaterialLibrary.h"
#include "ui/ScopedPanel.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <string_view>

namespace
{
    bool ContainsCaseInsensitive(std::string_view haystack, std::string_view needle)
    {
        if (needle.empty())
            return true;
        const auto it = std::search(
            haystack.begin(), haystack.end(), needle.begin(), needle.end(),
            [](char a, char b)
            { return std::tolower(static_cast<unsigned char>(a))
                  == std::tolower(static_cast<unsigned char>(b)); });
        return it != haystack.end();
    }

    // "materials/dev/gray" -> {"materials/dev", "gray"}
    std::pair<std::string_view, std::string_view> SplitFolder(std::string_view displayName)
    {
        const std::size_t slash = displayName.rfind('/');
        if (slash == std::string_view::npos)
            return { std::string_view{}, displayName };
        return { displayName.substr(0, slash), displayName.substr(slash + 1) };
    }
}

MaterialBrowserPanel::MaterialBrowserPanel(MaterialLibrary& materials,
                                           const MaterialEditSession& session,
                                           Actions actions)
    : Materials(materials)
    , Session(session)
    , Act(std::move(actions))
{
}

void MaterialBrowserPanel::OnDraw()
{
    if (!IsVisible())
        return;

    ScopedPanel panel(GetTitle().data(), &Visible);
    if (!panel.IsOpen())
        return;

    if (ImGui::Button("Rescan") && Act.Rescan)
        Act.Rescan();
    ImGui::SameLine();
    ImGui::TextDisabled("%zu material(s)", Materials.Materials().size());

    ImGui::Separator();
    ImGui::InputText("##name", NameBuffer, sizeof(NameBuffer));
    ImGui::SameLine();
    const bool nameValid = NameBuffer[0] != '\0';
    ImGui::BeginDisabled(!nameValid);
    if (ImGui::Button("New") && Act.CreateNew)
        Act.CreateNew(NameBuffer);
    ImGui::SameLine();
    ImGui::BeginDisabled(!Session.HasOpen());
    if (ImGui::Button("Duplicate") && Act.Duplicate)
        Act.Duplicate(NameBuffer);
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("New writes a default material; Duplicate copies the open one. Both land in materials/<name>.smat under the first content root.");

    ImGui::Separator();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##filter", "filter materials", FilterBuffer, sizeof(FilterBuffer));

    if (ImGui::BeginChild("##materials"))
        DrawMaterialList();
    ImGui::EndChild();
}

void MaterialBrowserPanel::DrawMaterialList()
{
    const std::string_view filter = FilterBuffer;

    // Filtering flattens: matches show with their full path, no tree to dig
    // through. Without a filter the list groups by folder (entries are sorted
    // by path, so folders are contiguous runs).
    if (!filter.empty())
    {
        for (const MaterialAsset& material : Materials.Materials())
        {
            if (!ContainsCaseInsensitive(material.DisplayName, filter))
                continue;
            const bool selected = Session.HasOpen() && Session.VirtualPath() == material.Path;
            if (ImGui::Selectable(material.DisplayName.c_str(), selected) && Act.Open)
                Act.Open(material.Path);
        }
        return;
    }

    std::string_view openFolder;
    bool folderVisible = true;
    bool folderStarted = false;
    for (const MaterialAsset& material : Materials.Materials())
    {
        const auto [folder, leaf] = SplitFolder(material.DisplayName);
        if (!folderStarted || folder != openFolder)
        {
            if (folderStarted && folderVisible && !openFolder.empty())
                ImGui::TreePop();
            openFolder = folder;
            folderStarted = true;
            if (folder.empty())
                folderVisible = true;
            else
                folderVisible = ImGui::TreeNodeEx(std::string(folder).c_str(),
                                                  ImGuiTreeNodeFlags_DefaultOpen
                                                      | ImGuiTreeNodeFlags_SpanAvailWidth);
        }
        if (!folderVisible)
            continue;

        const bool selected = Session.HasOpen() && Session.VirtualPath() == material.Path;
        // ID from the full path: two folders may both hold e.g. "wall".
        ImGui::PushID(material.Path.c_str());
        if (ImGui::Selectable(std::string(leaf).c_str(), selected) && Act.Open)
            Act.Open(material.Path);
        ImGui::PopID();
    }
    if (folderStarted && folderVisible && !openFolder.empty())
        ImGui::TreePop();
}
