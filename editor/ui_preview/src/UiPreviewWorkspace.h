#pragma once

#include "authoring/BindingMissProperties.h"
#include "authoring/DocumentLibrary.h"
#include "authoring/ElementOutline.h"
#include "authoring/ElementProperties.h"
#include "authoring/LibraryOutline.h"
#include "authoring/PreviewFraming.h"
#include "authoring/PreviewModelProperties.h"
#include "authoring/SurfaceProperties.h"
#include "authoring/UiPreviewModel.h"
#include "authoring/UiPreviewSession.h"
#include "authoring/VocabularyOutline.h"

#include "actions/ActionContext.h"
#include "render/UiSurfaceTargetRenderFeature.h"
#include "ui/WorkspaceView.h"
#include "vocabulary/VocabularyCatalog.h"
#include "workspaces/IWorkspace.h"

#include <math/geometry/2d/Rect2d.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class ActionCatalog;
class ContentPicture;
class Engine;
class Game;
class NoticeQueue;
class ProblemList;
class SourceReloadRoots;
struct ProjectDescriptor;

// The UI preview workspace: an authored document drawn by the engine's own UI
// pass at the resolution and scale the author chooses, filling the window and
// re-cooked on every save. Interact hands it the pointer and keys; Inspect
// picks its elements instead, each shown on the card with its boxes, computed
// style and attributes. With nothing picked the card is the surface and what
// the document asked its model for in vain, and the details are the preview
// model itself, saved beside the document. The document library, the element
// tree and the vocabulary are explorers, the layer's diagnostics are problems,
// and what the document raises is a notice.
class UiPreviewWorkspace final : public IWorkspace
{
public:
    // The module is loaded for its vocabulary alone and never started.
    UiPreviewWorkspace(Engine& engine, const ProjectDescriptor* project, Game* module, ActionCatalog& catalog,
                       ProblemList& problems, NoticeQueue& notices);
    ~UiPreviewWorkspace() override;

    UiPreviewWorkspace(const UiPreviewWorkspace&) = delete;
    UiPreviewWorkspace& operator=(const UiPreviewWorkspace&) = delete;

    void Tick(FrameUpdateContext& ctx) override;
    void SetVisible(bool visible) override;
    void HandlePlatformEvent(PlatformEventContext& ctx) override;
    [[nodiscard]] bool HoldsPointer() const override { return Panning; }
    void MoveTo(const WorkspaceWindow& window) override;
    void SetContentRect(const Rect2d& content) override { ContentRect = content; }
    [[nodiscard]] ActionContext* Actions() override { return &ActionHandlers; }
    [[nodiscard]] WorkspaceDocumentFacet Document() const override;
    [[nodiscard]] WorkspaceOperationFacet Operation() override;
    [[nodiscard]] WorkspaceViewFacet Viewing() const override;
    [[nodiscard]] WorkspaceSelectionFacet Selection() override;
    bool RevealProblem(const Problem& problem) override;
    [[nodiscard]] std::vector<WorkspaceExplorerInfo> Explorers() const override;
    [[nodiscard]] ExplorerPage Explore(const ExplorerQuery& query) override;
    void ChooseInExplorer(std::string_view explorer, std::string_view key, ExplorerChoice choice,
                          std::size_t mark) override;
    WorkspaceView& View() override { return Surface; }

private:
    void BuildLibrary(const ProjectDescriptor* project);
    void BuildUi();
    void DefineActions(ActionCatalog& catalog);
    void RegisterCommands();

    void OpenDocument(const std::string& packagePath);
    void RescanLibrary();
    bool SaveModel(std::string* error);
    // Every binding asset in the mounted content, inspected against the
    // vocabulary, and what is wrong with them reported.
    void ScanVocabulary();
    void OpenInEditor(const DocumentEntry& entry);
    void SetPointerMode(UiPreviewSession::PointerMode mode);
    void SetZoom(PreviewZoom zoom);
    [[nodiscard]] const DocumentEntry* OpenEntry() const;

    // Per frame: the picture placed and marked, the problems and notices the
    // session's histories add up to.
    void ShowPicture();
    void Report();
    [[nodiscard]] UiElementRef ElementAtWindowPoint(Vec2d point) const;

    Engine& EngineRef;
    SourceReloadRoots* Watch = nullptr;
    ProblemList& Problems;
    NoticeQueue& Notices;
    VocabularyCatalog Vocabulary;
    DocumentLibrary Library;
    std::unique_ptr<UiPreviewSession> Session;
    // The document's source, for the sidecar beside it.
    std::filesystem::path OpenSource;
    // What was open when the workspace went to the background: the screen
    // closes with it, and the working model comes back when it does.
    std::optional<std::pair<std::string, UiPreviewModel>> Suspended;

    UiSurfaceTargetRenderFeature* Target = nullptr;
    UiSurfaceTargetId Binding;
    std::unique_ptr<ContentPicture> Picture;
    Rect2d ContentRect{};
    bool Visible = false;
    PreviewZoom Zoom = PreviewZoom::Fit;
    Vec2d Pan{ 0.0f, 0.0f };
    bool SafeArea = false;
    // A middle drag on the content moves a zoomed surface until it lets go.
    bool Panning = false;

    UiElementRef Hovered;
    UiElementRef Selected;
    ElementOutline Outline;
    LibraryOutline Libraries;
    std::unique_ptr<ElementProperties> ElementCard;
    std::unique_ptr<SurfaceProperties> SurfaceCard;
    std::unique_ptr<PreviewModelProperties> ModelCard;
    std::unique_ptr<BindingMissProperties> MissCard;
    VocabularyOutline VocabularyExplorer;
    std::vector<BindingAsset> Bindings;

    ActionContext ActionHandlers;
    // What was last reported, so a change is reported once.
    std::uint64_t ReportedDiagnostics = 0;
    std::uint64_t ReportedRaised = 0;
    std::vector<Problem> Reported;

    // Declared last: its handlers reference everything above.
    WorkspaceView Surface;
};
