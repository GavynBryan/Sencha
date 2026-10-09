#pragma once

#include "data/DataCreation.h"
#include "DataEditorWorkspace.h"
#include "DataSourceText.h"

#include "actions/ActionCatalog.h"
#include "actions/ActionContext.h"
#include "data/DataProperties.h"
#include "data/DataSubtypeAuthoring.h"
#include "prompts/PromptQueue.h"
#include "properties/FilePathProperty.h"
#include "workspaces/IWorkspace.h"

#include <cstdint>
#include <string>

class ActionCatalog;
class Engine;
class ProblemList;
class ProjectFileSteps;
struct ProjectDescriptor;

// Ctrl+N: a new data asset, its subtype and place asked for first.
[[nodiscard]] ActionDefinition NewDataAssetAction();

// The data workspace: every structured data asset in the project, the open
// one presented whole as a sheet of its schema's fields (or as its source),
// the project's data assets in an explorer, and every open one on the
// document dial. A subtype reads its documents as what they are and may put
// previews beside them.
class DataWorkspace final : public IWorkspace
{
public:
    // Without `files`, documents are not moved, copied or trashed.
    DataWorkspace(Engine& engine, const ProjectDescriptor& project, DocumentSourceSet& sources,
                  DataDocumentStore& store, ProjectFileSteps* files, PromptQueue& prompts, ActionCatalog& catalog,
                  ProblemList& problems, const DataSubtypeAuthoringSetup& authorings);
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
    bool OpenAsset(const ProjectFile& file) override;
    [[nodiscard]] WorkspaceSelectionFacet Selection() override;
    bool RevealProblem(const Problem& problem) override;
private:
    // Opens the data asset at `assetPath`, or brings it forward, as the card.
    bool OpenPath(std::string_view assetPath);
    void BuildUi();
    void RegisterCommands();
    void DefineActions(ActionCatalog& catalog);
    void SaveActive();
    void DuplicateActive();
    void TrashActive();
    // Settles the active document's changes as the person says, then runs `then`.
    void AfterSettling(std::function<void(const std::string& path, DirtyDisposition disposition)> then);
    void PublishProblems();

    Engine& EngineRef;
    DataEditorWorkspace Model;
    ProjectFileSteps* Files;
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
};
