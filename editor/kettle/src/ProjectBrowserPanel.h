#pragma once

#include "ProjectCatalog.h"

#include "project/Project.h"
#include "ui/IEditorPanel.h"

#include <functional>
#include <optional>
#include <string>

// The launcher's single surface: recent projects (open in either editor),
// create-project form, and settings for the selected project. Catalog
// mutation and process launching stay behind callbacks the composition root
// wires; descriptor load/save is plain data I/O the panel does itself.
class ProjectBrowserPanel final : public IEditorPanel
{
public:
    struct Actions
    {
        std::function<void(const std::string& projectPath)> OpenLevelEditor;
        std::function<void(const std::string& projectPath)> OpenMaterialEditor;
        std::function<void()> BrowseForProject;
        std::function<void(const std::string& directory, const std::string& name)> CreateProject;
        std::function<void(const std::string& projectPath)> RemoveEntry;
        // After the panel saves settings (name may have changed).
        std::function<void(const ProjectDescriptor& descriptor, const std::string& path)> SettingsSaved;
    };

    ProjectBrowserPanel(const ProjectCatalog& catalog, Actions actions);

    [[nodiscard]] std::string_view GetTitle() const override { return "Projects"; }
    [[nodiscard]] DockSlot GetDockSlot() const override { return DockSlot::Center; }
    void OnDraw() override;

    // Opens the create-project modal on the next draw (File > New routes here).
    void RequestCreateProject() { CreatePopupRequested = true; }

private:
    void DrawRecentList();
    void DrawCreatePopup();
    void DrawSettingsSection();
    void SelectProject(const std::string& path);

    const ProjectCatalog& Catalog;
    Actions Act;

    // Settings editor state: the descriptor loaded for the selected entry.
    std::string SelectedPath;
    std::optional<ProjectDescriptor> Selected;
    std::string SelectedError;
    char NameBuffer[128] = "";
    char ModuleBuffer[512] = "";
    char NewRootBuffer[512] = "";

    // Create form state.
    bool CreatePopupRequested = false;
    char CreateDirBuffer[512] = "";
    char CreateNameBuffer[128] = "";
};
