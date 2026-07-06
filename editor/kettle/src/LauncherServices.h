#pragma once

#include "ProjectCatalog.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

class Engine;
class EngineSchedule;
class SdlWindow;
class EditorUiFeature;
struct PlatformEventContext;
struct ProjectDescriptor;

//=============================================================================
// LauncherServices
//
// Kettle's composition root: the recent-project catalog, the project browser
// panel, and the editor process launches. Children are spawned detached with
// "--project <path>" and survive the launcher exiting; pids are kept only for
// non-blocking zombie reaping while it runs.
//=============================================================================
class LauncherServices
{
public:
    LauncherServices(Engine& engine, SdlWindow& window);
    ~LauncherServices();

    LauncherServices(const LauncherServices&) = delete;
    LauncherServices& operator=(const LauncherServices&) = delete;

    void RegisterSystems(EngineSchedule& schedule);
    void HandlePlatformEvent(PlatformEventContext& ctx);

private:
    void BuildUi();
    void ProcessFrame();

    // Panel actions.
    void LaunchEditor(const char* binaryName, const std::string& projectPath);
    void BrowseForProject();
    void CreateProject(const std::string& directory, const std::string& name);
    void TouchCatalog(const std::string& projectPath);
    void RemoveCatalogEntry(const std::string& projectPath);
    void SaveCatalog();

    // Sibling editor binary: beside this executable in an installed SDK,
    // ../<name>/<name> in the build tree.
    [[nodiscard]] static std::string ResolveEditorBinary(const char* name);

    Engine* EnginePtr = nullptr;
    SdlWindow* Window = nullptr;

    ProjectCatalog Catalog;
    EditorUiFeature* UiFeature = nullptr;
    std::vector<long> ChildPids;

    // Browse dialog results land off the frame loop; applied in ProcessFrame.
    std::mutex PendingMutex;
    std::vector<std::string> PendingBrowsedProjects;
};
