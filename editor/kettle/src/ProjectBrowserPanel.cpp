#include "ProjectBrowserPanel.h"

#include "ui/ScopedPanel.h"

#include <imgui.h>

#include <cstring>
#include <filesystem>

namespace
{
    void CopyToBuffer(char* buffer, std::size_t size, const std::string& value)
    {
        std::snprintf(buffer, size, "%s", value.c_str());
    }
}

ProjectBrowserPanel::ProjectBrowserPanel(const ProjectCatalog& catalog, Actions actions)
    : Catalog(catalog)
    , Act(std::move(actions))
{
}

void ProjectBrowserPanel::SelectProject(const std::string& path)
{
    SelectedPath = path;
    SelectedError.clear();
    Selected.reset();

    ProjectDescriptor descriptor;
    std::string error;
    if (ProjectDescriptor::Load(path, descriptor, &error))
    {
        Selected = std::move(descriptor);
        CopyToBuffer(NameBuffer, sizeof(NameBuffer), Selected->Name);
        CopyToBuffer(ModuleBuffer, sizeof(ModuleBuffer), Selected->GameModulePath);
        NewRootBuffer[0] = '\0';
    }
    else
    {
        SelectedError = error;
    }
}

void ProjectBrowserPanel::DrawRecentList()
{
    ImGui::SeparatorText("Recent Projects");

    if (ImGui::Button("New Project..."))
        RequestCreateProject();
    ImGui::SameLine();
    if (ImGui::Button("Browse...") && Act.BrowseForProject)
        Act.BrowseForProject();

    if (Catalog.Entries().empty())
    {
        ImGui::TextDisabled("No projects yet. Browse for a .senchaproj or create one below.");
        return;
    }

    if (!ImGui::BeginTable("##projects", 3,
                           ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
        return;
    ImGui::TableSetupColumn("Project", ImGuiTableColumnFlags_WidthStretch, 0.45f);
    ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch, 0.35f);
    ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthStretch, 0.20f);

    for (const ProjectCatalogEntry& entry : Catalog.Entries())
    {
        std::error_code ec;
        const bool missing = !std::filesystem::exists(entry.Path, ec);

        ImGui::PushID(entry.Path.c_str());
        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        const std::string label = entry.Name.empty() ? entry.Path : entry.Name;
        if (ImGui::Selectable(label.c_str(), SelectedPath == entry.Path,
                              ImGuiSelectableFlags_SpanAllColumns
                                  | ImGuiSelectableFlags_AllowOverlap))
            SelectProject(entry.Path);
        if (missing)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("(missing)");
        }

        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", entry.Path.c_str());

        ImGui::TableNextColumn();
        ImGui::BeginDisabled(missing);
        if (ImGui::SmallButton("Kyusu") && Act.OpenLevelEditor)
            Act.OpenLevelEditor(entry.Path);
        ImGui::SetItemTooltip("Open in the level editor");
        ImGui::SameLine();
        if (ImGui::SmallButton("Chakin") && Act.OpenMaterialEditor)
            Act.OpenMaterialEditor(entry.Path);
        ImGui::SetItemTooltip("Open in the material editor");
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::SmallButton("x") && Act.RemoveEntry)
            Act.RemoveEntry(entry.Path);
        ImGui::SetItemTooltip("Remove from the recent list (the project itself is untouched)");

        ImGui::PopID();
    }
    ImGui::EndTable();
}

void ProjectBrowserPanel::DrawCreatePopup()
{
    if (CreatePopupRequested)
    {
        ImGui::OpenPopup("Create Project");
        CreatePopupRequested = false;
    }

    if (!ImGui::BeginPopupModal("Create Project", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::InputTextWithHint("Directory", "/path/to/new/project", CreateDirBuffer,
                             sizeof(CreateDirBuffer));
    ImGui::InputTextWithHint("Name", "defaults to the directory name", CreateNameBuffer,
                             sizeof(CreateNameBuffer));
    ImGui::TextDisabled("Writes <directory>/project.senchaproj with a default game\nmodule path and an assets/ content root.");

    ImGui::BeginDisabled(CreateDirBuffer[0] == '\0');
    if (ImGui::Button("Create"))
    {
        if (Act.CreateProject)
            Act.CreateProject(CreateDirBuffer, CreateNameBuffer);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void ProjectBrowserPanel::DrawSettingsSection()
{
    if (SelectedPath.empty())
        return;

    ImGui::SeparatorText("Project Settings");
    ImGui::TextDisabled("%s", SelectedPath.c_str());

    if (!SelectedError.empty())
    {
        ImGui::TextUnformatted(SelectedError.c_str());
        return;
    }
    if (!Selected)
        return;

    ImGui::InputText("Name", NameBuffer, sizeof(NameBuffer));
    ImGui::InputText("Game Module", ModuleBuffer, sizeof(ModuleBuffer));

    ImGui::TextUnformatted("Content Roots");
    int removeIndex = -1;
    for (int i = 0; i < static_cast<int>(Selected->ContentRoots.size()); ++i)
    {
        ImGui::PushID(i);
        ImGui::Bullet();
        ImGui::SameLine();
        ImGui::TextUnformatted(Selected->ContentRoots[static_cast<std::size_t>(i)].c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove"))
            removeIndex = i;
        ImGui::PopID();
    }
    if (removeIndex >= 0)
        Selected->ContentRoots.erase(Selected->ContentRoots.begin() + removeIndex);

    ImGui::InputTextWithHint("##newroot", "add content root (absolute or project-relative)",
                             NewRootBuffer, sizeof(NewRootBuffer));
    ImGui::SameLine();
    ImGui::BeginDisabled(NewRootBuffer[0] == '\0');
    if (ImGui::Button("Add"))
    {
        // Descriptor fields are absolute in memory; Save relativizes what it can.
        std::filesystem::path root(NewRootBuffer);
        if (root.is_relative())
            root = std::filesystem::path(Selected->Directory) / root;
        Selected->ContentRoots.push_back(root.lexically_normal().string());
        NewRootBuffer[0] = '\0';
    }
    ImGui::EndDisabled();

    if (ImGui::Button("Save Settings"))
    {
        Selected->Name = NameBuffer;
        Selected->GameModulePath = ModuleBuffer;
        std::string error;
        if (Selected->Save(SelectedPath, &error))
        {
            if (Act.SettingsSaved)
                Act.SettingsSaved(*Selected, SelectedPath);
        }
        else
        {
            SelectedError = error;
        }
    }
}

void ProjectBrowserPanel::OnDraw()
{
    if (!IsVisible())
        return;

    ScopedPanel panel(GetTitle(), &Visible);
    if (!panel.IsOpen())
        return;

    DrawRecentList();
    ImGui::Spacing();
    DrawSettingsSection();
    DrawCreatePopup();
}
