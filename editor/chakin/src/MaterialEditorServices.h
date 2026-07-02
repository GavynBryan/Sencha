#pragma once

#include "MaterialTabSet.h"

#include "project/MaterialLibrary.h"
#include "project/Project.h"

#include <core/assets/RuntimeAssets.h>

#include <cstddef>
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
// builds the browse/inspect/preview panels over a MaterialTabSet (one edit
// session + undo history per open material), and pushes each tab's working
// description into its resident material after every change so the preview
// (and, through the saved file, a running level editor) is always live.
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

    // Browser/tab actions. New/Duplicate write materials/<name>.smat under the
    // first content root, re-register it, and open it.
    void OpenMaterial(const std::string& virtualPath);
    void CloseTab(std::size_t index);
    void SaveActiveMaterial();
    void SaveAllMaterials();
    void CreateMaterial(const std::string& name, bool duplicateOpen);
    // Moves the .smat on disk to content-root-relative newRelPath (".smat"
    // appended when missing) and re-points any open tab. Refs in levels are
    // not rewritten; they fall back to the level default until reassigned.
    void RenameMaterial(const std::string& virtualPath, const std::string& newRelPath);
    void RescanMaterials();

    // Pushes a tab's working description into its resident material (in-place
    // swap; live handles keep working).
    void ApplyWorkingToResident(MaterialEditTab& tab);
    void UpdateTitle();

    Engine* EnginePtr = nullptr;
    SdlWindow* Window = nullptr;
    std::optional<std::string> ProjectPath;
    std::optional<ProjectDescriptor> Project;

    std::optional<RuntimeAssets> Assets;
    std::unique_ptr<MaterialLibrary> Materials;
    MaterialTabSet Tabs;

    EditorUiFeature* UiFeature = nullptr;
    MaterialPreviewRenderFeature* Preview = nullptr;
    std::string LastWindowTitle;
};
