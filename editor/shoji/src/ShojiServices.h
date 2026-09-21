#pragma once

#include "authoring/DocumentLibrary.h"
#include "authoring/VocabularyCatalog.h"
#include "authoring/UiPreviewSession.h"
#include "ui/PreviewViewState.h"

#include "project/Project.h"
#include "render/UiSurfaceTargetRenderFeature.h"

#include <app/GameModuleLoader.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

class EditorUiFeature;
class Engine;
class EngineSchedule;
class SdlWindow;
class SourceReloadRoots;
struct EngineConfig;
struct PlatformEventContext;

//=============================================================================
// ShojiServices
//
// The previewer's composition root: mounts the libraries of authored documents
// into the engine's asset stack (the one its UI layer resolves through), keeps
// them re-cooking as their sources change, holds the one preview session and
// the render target that shows it, and wires the panels over both.
//
// Member order is dependency order; the destructor takes the render feature
// out while the caches it borrows are still alive.
//=============================================================================
class ShojiServices
{
public:
    ShojiServices(Engine& engine,
                  SdlWindow& window,
                  const EngineConfig& config,
                  std::optional<std::string> projectPath,
                  std::optional<std::string> initialDocument);
    ~ShojiServices();

    ShojiServices(const ShojiServices&) = delete;
    ShojiServices& operator=(const ShojiServices&) = delete;

    void RegisterSystems(EngineSchedule& schedule);
    void HandlePlatformEvent(PlatformEventContext& ctx);

private:
    void LoadProject();
    void LoadVocabulary();
    void MountLibraries();
    void BuildSourceWatch();
    void BuildUi();
    void ProcessFrame();

    void OpenDocument(const std::string& packagePath);
    void RescanLibrary();
    bool SaveModel(std::string* error);
    void ResetModel();
    void OpenInEditor(const DocumentEntry& entry);
    void UpdateTitle();

    Engine* EnginePtr = nullptr;
    SdlWindow* Window = nullptr;
    std::optional<std::string> ProjectPath;
    std::optional<std::string> InitialDocument;
    std::optional<ProjectDescriptor> Project;

    // The project's module, loaded for its declarations only, and the metadata
    // World those declarations fill. The catalog is destroyed before the
    // module is unmapped: a declaration can carry module code.
    GameModuleLoader ModuleLoader;
    LoadedModule GameModule;
    std::unique_ptr<VocabularyCatalog> Vocabulary;

    DocumentLibrary Library;
    std::unique_ptr<SourceReloadRoots> Watch;
    std::unique_ptr<UiPreviewSession> Session;
    PreviewViewState View;
    // The document's source, for the sidecar beside it.
    std::filesystem::path OpenSource;
    bool EditorThemeApplied = false;

    EditorUiFeature* UiFeature = nullptr;
    UiSurfaceTargetRenderFeature* Target = nullptr;
    UiSurfaceTargetId Binding;
    std::string LastWindowTitle;
};
