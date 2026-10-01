#pragma once

#include "data/DataCreation.h"
#include "DataEditorWorkspace.h"
#include "DataSourceText.h"

#include "actions/ActionContext.h"
#include "data/DataProperties.h"
#include "data/DataSubtypeAuthoring.h"
#include "prompts/PromptQueue.h"
#include "properties/FilePathProperty.h"
#include "ui/WorkspaceView.h"
#include "workspaces/IWorkspace.h"

#include <cstdint>
#include <string>

class ActionCatalog;
class Engine;
class ProblemList;
struct ProjectDescriptor;

// The data workspace: every structured data asset in the project, the open
// one presented whole as a sheet of its schema's fields (or as its source),
// the project's data assets in an explorer, and every open one on the
// document dial. A subtype reads its documents as what they are and may put
// previews beside them.
class DataWorkspace final : public IWorkspace
{
public:
    DataWorkspace(Engine& engine, const ProjectDescriptor& project, DocumentSourceSet& sources,
                  DataDocumentStore& store, PromptQueue& prompts, ActionCatalog& catalog, ProblemList& problems,
                  const DataSubtypeAuthoringSetup& authorings);
    ~DataWorkspace() override;

    DataWorkspace(const DataWorkspace&) = delete;
    DataWorkspace& operator=(const DataWorkspace&) = delete;

    void Tick(FrameUpdateContext& ctx) override;
    void SetVisible(bool visible) override { Visible = visible; }
    [[nodiscard]] bool ClaimPlatformEvent(PlatformEventContext& ctx) override;
    [[nodiscard]] bool OwnsDocument(const DocumentRef& document) const override;
    void RevealDocument(const DocumentRef& document) override;
    [[nodiscard]] ActionContext* Actions() override { return &ActionHandlers; }
    [[nodiscard]] WorkspaceDocumentFacet Document() const override;
    void ShowDocument(std::string_view key) override;
    [[nodiscard]] WorkspaceSelectionFacet Selection() override;
    bool RevealProblem(const Problem& problem) override;
    [[nodiscard]] std::vector<WorkspaceExplorerInfo> Explorers() const override;
    [[nodiscard]] ExplorerPage Explore(const ExplorerQuery& query) override;
    void ChooseInExplorer(std::string_view explorer, std::string_view key, ExplorerChoice choice,
                          std::size_t mark) override;
    WorkspaceView& View() override { return Surface; }

private:
    void BuildUi();
    void RegisterCommands();
    void DefineActions(ActionCatalog& catalog);
    void SaveActive();
    void DuplicateActive();
    // Settles the active document's changes as the person says, then runs `then`.
    void AfterSettling(std::function<void(const std::string& path, DirtyDisposition disposition)> then);
    void PublishProblems();

    Engine& EngineRef;
    DataEditorWorkspace Model;
    PromptQueue::Asker Prompts;
    ProblemList& Problems;
    ActionContext ActionHandlers;
    // What subtypes add to their schema's properties; the properties read it.
    DataSubtypeAuthoringRegistry Authoring;
    DataProperties Properties;
    DataSourceText Source;
    DataCreation Creation;
    FilePathProperty FilePath;
    // What the sheet is about: the active document, or a new asset being made.
    // Only the new-asset action and showing a document change it.
    enum class CardSubject : std::uint8_t
    {
        Document,
        Creating,
    };
    CardSubject Subject = CardSubject::Document;
    // The active document shown as its source rather than as fields.
    bool ShowingSource = false;
    bool Visible = false;
    std::string OperationError;
    // Declared last: its panels reference everything above.
    WorkspaceView Surface;
};
