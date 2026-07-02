#pragma once

#include "MaterialEditSession.h"

#include "commands/CommandStack.h"
#include "project/MaterialLibrary.h"
#include "project/Project.h"

#include <core/assets/RuntimeAssets.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

class Engine;
class EngineSchedule;
class SdlWindow;
class EditorUiFeature;
class MaterialPreviewRenderFeature;
struct PlatformEventContext;
struct EngineConfig;

//=============================================================================
// MaterialEditorServices
//
// The material editor's composition root: mounts the project's content,
// builds the browse/inspect/preview panels over one MaterialEditSession, and
// pushes the session's working description into the resident material after
// every change so the preview (and, through the saved file, a running level
// editor) is always live.
//=============================================================================
class MaterialEditorServices
{
public:
    MaterialEditorServices(Engine& engine,
                           SdlWindow& window,
                           const EngineConfig& config,
                           std::optional<std::string> projectPath);
    ~MaterialEditorServices();

    MaterialEditorServices(const MaterialEditorServices&) = delete;
    MaterialEditorServices& operator=(const MaterialEditorServices&) = delete;

    void RegisterSystems(EngineSchedule& schedule);
    void HandlePlatformEvent(PlatformEventContext& ctx);

private:
    void LoadProject();
    void InitAssets();
    void BuildUi();
    void ProcessFrame();

    // Browser actions. New/Duplicate write materials/<name>.smat under the
    // first content root, re-register it, and open it.
    void OpenMaterial(const std::string& virtualPath);
    void SaveOpenMaterial();
    void CreateMaterial(const std::string& name, bool duplicateOpen);
    void RescanMaterials();

    // Pushes the session's working description into the resident material
    // (in-place swap; live handles keep working).
    void ApplyWorkingToResident();
    void UpdateTitle();

    Engine* EnginePtr = nullptr;
    SdlWindow* Window = nullptr;
    std::optional<std::string> ProjectPath;
    std::optional<ProjectDescriptor> Project;

    std::optional<RuntimeAssets> Assets;
    std::unique_ptr<MaterialLibrary> Materials;
    CommandStack Commands;
    MaterialEditSession Session;
    uint64_t AppliedSessionVersion = 0;

    EditorUiFeature* UiFeature = nullptr;
    MaterialPreviewRenderFeature* Preview = nullptr;
    MaterialHandle OpenMaterialHandle{};
    std::string LastWindowTitle;
};
