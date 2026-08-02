#pragma once

#include "DataEditorWorkspace.h"

#include "project/Project.h"

#include <app/GameModuleLoader.h>
#include <assets/runtime/RuntimeAssets.h>

#include <memory>
#include <optional>
#include <string>

class Engine;
class EngineSchedule;
class SdlWindow;
class EditorUiFeature;
struct EngineConfig;
struct PlatformEventContext;

class DataEditorServices
{
public:
    DataEditorServices(Engine& engine,
                       SdlWindow& window,
                       const EngineConfig& config,
                       std::optional<std::string> projectPath,
                       std::optional<std::string> initialAsset);
    ~DataEditorServices();

    DataEditorServices(const DataEditorServices&) = delete;
    DataEditorServices& operator=(const DataEditorServices&) = delete;

    void RegisterSystems(EngineSchedule& schedule);
    void HandlePlatformEvent(PlatformEventContext& ctx);

private:
    void LoadProject();
    void InitAssets();
    void BuildUi();
    void ProcessFrame();
    void UpdateTitle();

    Engine* EnginePtr = nullptr;
    SdlWindow* Window = nullptr;
    std::optional<std::string> ProjectPath;
    std::optional<std::string> InitialAsset;
    std::optional<ProjectDescriptor> Project;

    std::optional<RuntimeAssets> Assets;
    GameModuleLoader ModuleLoader;
    LoadedModule ProjectModule;
    std::unique_ptr<DataEditorWorkspace> Workspace;

    EditorUiFeature* UiFeature = nullptr;
    std::string LastWindowTitle;
};
