#include "MaterialBrowserPanel.h"

#include "project/MaterialLibrary.h"
#include "ui/ScopedPanel.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
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
                                           MaterialTabSet& tabs,
                                           Actions actions)
    : Materials(materials)
    , Tabs(tabs)
    , Act(std::move(actions))
{
}

void MaterialBrowserPanel::OnDraw()
{
    if (!IsVisible())
        return;

    ScopedPanel panel(GetTitle(), &Visible);
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
    const MaterialEditTab* active = Tabs.Active();
    ImGui::BeginDisabled(!nameValid);
    if (ImGui::Button("New") && Act.CreateNew)
        Act.CreateNew(NameBuffer);
    ImGui::SameLine();
    ImGui::BeginDisabled(active == nullptr || !active->Session.HasOpen());
    if (ImGui::Button("Duplicate") && Act.Duplicate)
        Act.Duplicate(NameBuffer);
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("New writes a default material; Duplicate copies the active tab. Both land in materials/<name>.smat under the first content root.");

    ImGui::Separator();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##filter", "filter materials", FilterBuffer, sizeof(FilterBuffer));

    if (ImGui::BeginChild("##materials"))
        DrawMaterialList();
    ImGui::EndChild();

    DrawRenamePopup();
}

void MaterialBrowserPanel::DrawMaterialRow(const char* label, const std::string& virtualPath,
                                           const std::string& displayName)
{
    const bool selected = Tabs.Find(virtualPath) != nullptr;
    ImGui::PushID(virtualPath.c_str());
    if (ImGui::Selectable(label, selected) && Act.Open)
        Act.Open(virtualPath);
    if (ImGui::BeginPopupContextItem("row_context"))
    {
        if (ImGui::MenuItem("Rename / Move..."))
        {
            RenameTarget = virtualPath;
            std::snprintf(RenameBuffer, sizeof(RenameBuffer), "%s", displayName.c_str());
            RenamePopupPending = true;
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
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
            DrawMaterialRow(material.DisplayName.c_str(), material.Path, material.DisplayName);
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

        DrawMaterialRow(std::string(leaf).c_str(), material.Path, material.DisplayName);
    }
    if (folderStarted && folderVisible && !openFolder.empty())
        ImGui::TreePop();
}

void MaterialBrowserPanel::DrawRenamePopup()
{
    if (RenamePopupPending)
    {
        ImGui::OpenPopup("Rename Material");
        RenamePopupPending = false;
    }

    if (!ImGui::BeginPopupModal("Rename Material", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::TextDisabled("%s", RenameTarget.c_str());
    ImGui::InputTextWithHint("New path", "materials/walls/brick", RenameBuffer,
                             sizeof(RenameBuffer));
    ImGui::TextDisabled("Content-root-relative; folders are created as needed.\nLevels referencing the old path fall back to the level default.");

    ImGui::BeginDisabled(RenameBuffer[0] == '\0');
    if (ImGui::Button("Rename"))
    {
        if (Act.Rename)
            Act.Rename(RenameTarget, RenameBuffer);
        RenameTarget.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
        RenameTarget.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}
