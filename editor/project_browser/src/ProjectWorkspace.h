#pragma once

#include "ProjectCatalog.h"
#include "ui/WorkspaceView.h"
#include "workspaces/IWorkspace.h"

#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

class Engine;
class ProjectBrowserPanel;
class SdlWindow;
struct ProjectDescriptor;

// Choosing and making projects: the recent-project catalog, the create form,
// and each project's settings. Opening one starts the application again on it
// and ends this process.
class ProjectWorkspace final : public IWorkspace
{
public:
    // `executable` is the application that opens a chosen project, beside
    // this one's binary.
    ProjectWorkspace(Engine& engine, SdlWindow& window, std::string executable);
    ~ProjectWorkspace() override;

    ProjectWorkspace(const ProjectWorkspace&) = delete;
    ProjectWorkspace& operator=(const ProjectWorkspace&) = delete;

    void Tick(FrameUpdateContext& ctx) override;
    WorkspaceView& View() override { return Surface; }

private:
    void OpenProject(const std::string& projectPath);
    void BrowseForProject();
    // `templateName` names a directory under the SDK's templates, or is empty
    // for a bare descriptor.
    void CreateProject(const std::string& directory, const std::string& name,
                       const std::string& templateName);
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

    // Browse dialog results land off the frame loop; applied in Tick.
    std::mutex PendingMutex;
    std::vector<std::string> PendingBrowsedProjects;

    WorkspaceView Surface;
};
