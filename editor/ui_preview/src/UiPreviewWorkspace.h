#pragma once

#include "authoring/DocumentLibrary.h"
#include "authoring/UiPreviewModel.h"
#include "authoring/UiPreviewSession.h"
#include "ui/PreviewViewState.h"
#include "ui/WorkspaceView.h"
#include "vocabulary/VocabularyCatalog.h"
#include "workspaces/IWorkspace.h"

#include "render/UiSurfaceTargetRenderFeature.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

class EditorUiFeature;
class Engine;
class Game;
class SourceReloadRoots;
struct ProjectDescriptor;

// The UI preview workspace: a live view of an authored document through the
// engine's own UI pass, re-cooked on every save, with its elements, preview
// model, raised actions and diagnostics beside it.
class UiPreviewWorkspace final : public IWorkspace
{
public:
    // The module is loaded for its vocabulary alone and never started.
    UiPreviewWorkspace(Engine& engine, const ProjectDescriptor* project, Game* module);
    ~UiPreviewWorkspace() override;

    UiPreviewWorkspace(const UiPreviewWorkspace&) = delete;
    UiPreviewWorkspace& operator=(const UiPreviewWorkspace&) = delete;

    void Tick(FrameUpdateContext& ctx) override;
    void SetVisible(bool visible) override;
    void HandlePlatformEvent(PlatformEventContext& ctx) override;
    WorkspaceView& View() override { return Surface; }
    void Place(EditorUiFeature& window) override { Window = &window; }

private:
    void BuildLibrary(const ProjectDescriptor* project);
    void BuildUi();
    void RegisterCommands();

    void OpenDocument(const std::string& packagePath);
    void RescanLibrary();
    bool SaveModel(std::string* error);
    void ResetModel();
    void OpenInEditor(const DocumentEntry& entry);

    Engine& EngineRef;
    EditorUiFeature* Window = nullptr;
    SourceReloadRoots* Watch = nullptr;
    VocabularyCatalog Vocabulary;
    DocumentLibrary Library;
    std::unique_ptr<UiPreviewSession> Session;
    PreviewViewState ViewState;
    // The document's source, for the sidecar beside it.
    std::filesystem::path OpenSource;
    // What was open when the tab went to the background: the screen closes
    // with it, and the working model comes back when the tab does.
    std::optional<std::pair<std::string, UiPreviewModel>> Suspended;

    UiSurfaceTargetRenderFeature* Target = nullptr;
    UiSurfaceTargetId Binding;

    // Declared last: its panels reference everything above.
    WorkspaceView Surface;
};
