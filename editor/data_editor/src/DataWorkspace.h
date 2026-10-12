#pragma once

#include "data/DataCreation.h"
#include "DataEditorWorkspace.h"
#include "DataSourceText.h"

#include "actions/ActionCatalog.h"
#include "actions/ActionContext.h"
#include "data/DataDocumentSheet.h"
#include "data/DataSubtypeAuthoring.h"
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
// and every open one on the document dial.
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
    void DefineActions(ActionCatalog& catalog);
    void SaveActive();
    void DuplicateActive();
    void TrashActive();
    // The card turns to the active document, as its fields.
    void ShowSheet();

    Engine& EngineRef;
    DataEditorWorkspace Model;
    ProjectFileSteps* Files;
    ProblemList& Problems;
    ActionContext ActionHandlers;
    DataDocumentSheet Sheet;
    DataSourceText Source;
    DataCreation Creation;
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
};
