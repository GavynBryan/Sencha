#pragma once

#include "ProjectCatalog.h"
#include "ProjectCreation.h"
#include "ProjectSettings.h"
#include "actions/ActionContext.h"
#include "ui/WorkspaceView.h"
#include "workspaces/IWorkspace.h"

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class ActionCatalog;
class Engine;
class ProjectRack;
class SdlWindow;
struct ProjectDescriptor;

// Choosing and making projects: the recent projects as a rack, the one picked
// shown with its settings, and a new one made from a card. Opening one starts
// the application again on it and ends this process.
class ProjectWorkspace final : public IWorkspace
{
public:
    // `executable` is the application that opens a chosen project, beside
    // this one's binary.
    ProjectWorkspace(Engine& engine, SdlWindow& window, ActionCatalog& catalog, std::string executable);
    ~ProjectWorkspace() override;

    ProjectWorkspace(const ProjectWorkspace&) = delete;
    ProjectWorkspace& operator=(const ProjectWorkspace&) = delete;

    void Tick(FrameUpdateContext& ctx) override;
    void SetVisible(bool visible) override;
    void MoveTo(const WorkspaceWindow& window) override;
    [[nodiscard]] ActionContext* Actions() override { return &ActionHandlers; }
    [[nodiscard]] WorkspaceSelectionFacet Selection() override;
    WorkspaceView& View() override { return Surface; }

private:
    void DefineActions(ActionCatalog& catalog);
    void BeginNewProject();
    void Pick(const std::string& projectPath);
    void OpenProject(const std::string& projectPath);
    void BrowseForProject();
    bool CreateProject(const std::string& directory, const std::string& name, const std::string& templateName,
                       std::string& error);
    void TouchCatalog(const std::string& projectPath);
    void RemoveCatalogEntry(const std::string& projectPath);
    void SaveCatalog();

    // Where the starter templates are: share/sencha/templates beside the
    // installed binaries, or the repository's templates/ when running in-tree.
    // Empty when neither is found.
    [[nodiscard]] static std::filesystem::path ResolveTemplatesDirectory();
    [[nodiscard]] static std::vector<std::string> ListTemplates();

    Engine& EngineRef;
    SdlWindow& Window;
    std::string Executable;
    ProjectCatalog Catalog;
    ActionContext ActionHandlers;
    std::unique_ptr<ProjectRack> Rack;
    ProjectSettings Settings;
    ProjectCreation Creation;
    std::string Picked;
    bool Creating = false;

    // Browse dialog results land off the frame loop; applied in Tick.
    std::mutex PendingMutex;
    std::vector<std::string> PendingBrowsedProjects;

    WorkspaceView Surface;
};
