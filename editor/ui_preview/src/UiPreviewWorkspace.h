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
#include "documents/SnapshotDocument.h"
#include "prompts/PromptQueue.h"
#include "render/UiSurfaceTargetRenderFeature.h"
#include "vocabulary/VocabularyCatalog.h"
#include "workspaces/ExplorerTable.h"
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
class DocumentSourceSet;
class Engine;
class Game;
class NoticeQueue;
class ProblemList;
class SourceReloadRoots;
struct ProjectDescriptor;

// The UI preview workspace (editor/ARCHITECTURE.md): an authored document
// drawn by the engine's own UI pass, inspected or interacted with, against a
// preview model kept as a journal document beside it.
class UiPreviewWorkspace final : public IWorkspace
{
public:
    // The module is loaded for its vocabulary alone and never started.
    UiPreviewWorkspace(Engine& engine, const ProjectDescriptor* project, Game* module, DocumentSourceSet& sources,
                       PromptQueue& prompts, ActionCatalog& catalog, ProblemList& problems, NoticeQueue& notices);
    ~UiPreviewWorkspace() override;

    UiPreviewWorkspace(const UiPreviewWorkspace&) = delete;
    UiPreviewWorkspace& operator=(const UiPreviewWorkspace&) = delete;

    void Tick(FrameUpdateContext& ctx) override;
    void SetVisible(bool visible) override;
    void HandlePlatformEvent(PlatformEventContext& ctx) override;
    void MoveTo(const WorkspaceWindow& window) override;
    void SetContentRect(const Rect2d& content) override { ContentRect = content; }
    [[nodiscard]] bool OwnsDocument(const DocumentRef& document) const override;
    void RevealDocument(const DocumentRef& document) override;
    [[nodiscard]] ActionContext* Actions() override { return &ActionHandlers; }
    [[nodiscard]] WorkspaceDocumentFacet Document() const override;
    [[nodiscard]] WorkspaceOperationFacet Operation() override;
    [[nodiscard]] WorkspaceViewFacet Viewing() const override;
    [[nodiscard]] WorkspaceSelectionFacet Selection() override;
    bool RevealProblem(const Problem& problem) override;
    bool OpenAsset(const ProjectFile& file) override;
    [[nodiscard]] ExplorerTable* Explorers() override { return &ExplorerRows; }

private:
    void BuildLibrary(const ProjectDescriptor* project);
    void BuildUi();
    void DefineActions(ActionCatalog& catalog);
    void DefineExplorers(ActionCatalog& catalog);

    // Asks first when the open model has unsaved changes.
    void OpenDocument(const std::string& packagePath);
    void LoadDocument(const std::string& packagePath);
    void RescanLibrary();
    void SaveModel();
    void RevertModel();
    // Every binding asset in the mounted content, inspected against the
    // vocabulary, and what is wrong with them reported.
    void ScanVocabulary();
    void OpenInEditor(const DocumentEntry& entry);
    void SetPointerMode(UiPreviewSession::PointerMode mode);
    void SetZoom(PreviewZoom zoom);
    [[nodiscard]] const DocumentEntry* OpenEntry() const;

    // Per frame: the picture placed and marked, and the problems and notices
    // the session's histories add up to.
    void ShowPicture();
    void Report();
    [[nodiscard]] UiElementRef ElementAtWindowPoint(Vec2d point) const;

    Engine& EngineRef;
    DocumentSourceSet& Sources;
    PromptQueue::Asker Prompts;
    SourceReloadRoots* Watch = nullptr;
    ProblemList& Problems;
    NoticeQueue& Notices;
    VocabularyCatalog Vocabulary;
    DocumentLibrary Library;
    std::unique_ptr<UiPreviewSession> Session;
    // The open document's preview model, as its sidecar's journal document.
    std::unique_ptr<SnapshotDocument> Model;
    // The screen closes while the workspace is in the background and opens
    // again on the session's model when it comes back.
    bool Suspended = false;
    std::string SetupProblem;

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
    ExplorerTable ExplorerRows;
    // What was last reported, so a change is reported once.
    std::uint64_t ReportedDiagnostics = 0;
    std::uint64_t ReportedRaised = 0;
    std::vector<Problem> Reported;
};
