#pragma once

#include "actions/ActionCatalog.h"
#include "actions/ActionContext.h"
#include "data/DataSubtypeAuthoring.h"
#include "prompts/PromptQueue.h"
#include "workspaces/ExplorerTable.h"
#include "workspaces/IWorkspace.h"
#include "workspaces/TreeOutline.h"
#include "workspaces/WorkspaceTimeline.h"

#include <math/geometry/2d/Rect2d.h>

#include <cstdint>
#include <memory>
#include <string>

class AbilityArgumentProperties;
class AbilityOverviewProperties;
class AbilityPhaseStrip;
class AbilityPreviewControls;
class AbilityPreviewInspector;
class AbilityPreviewSession;
class AbilityProblemProperties;
class AbilityWorkbench;
class ActionContextStack;
struct AbilityPaletteEntry;
class DataCreation;
class DataDocument;
class DataDocumentSheet;
class DataDocumentStore;
class DataProperties;
class DocumentSourceSet;
class Engine;
class Game;
class ProblemList;
class ProjectFileSteps;
struct ProjectDescriptor;

// A new ability, its recipe and place asked for first.
[[nodiscard]] ActionDefinition NewAbilityAction();

// The Ability Workbench (docs/editor/ability-workbench.md): an ability's phases,
// ops and transitions authored as elements selected by key, checked against the
// project's names as it is edited.
class AbilityWorkspace final : public IWorkspace, public IWorkspaceTimeline
{
public:
    AbilityWorkspace(Engine& engine, const ProjectDescriptor& project, Game* module, DocumentSourceSet& sources,
                     DataDocumentStore& store, ProjectFileSteps* files, PromptQueue& prompts, ActionCatalog& catalog,
                     ProblemList& problems, const DataSubtypeAuthoringSetup& authorings);
    ~AbilityWorkspace() override;

    AbilityWorkspace(const AbilityWorkspace&) = delete;
    AbilityWorkspace& operator=(const AbilityWorkspace&) = delete;

    void Tick(FrameUpdateContext& ctx) override;
    void SetVisible(bool visible) override;
    void MoveTo(const WorkspaceWindow& window) override;
    void SetContentRect(const Rect2d& content) override { ContentRect = content; }
    [[nodiscard]] bool ClaimPlatformEvent(PlatformEventContext& ctx) override;
    void HandlePlatformEvent(PlatformEventContext& ctx) override;
    [[nodiscard]] bool OwnsDocument(const DocumentRef& document) const override;
    void RevealDocument(const DocumentRef& document) override;
    [[nodiscard]] ActionContext* Actions() override { return &ActionHandlers; }
    [[nodiscard]] WorkspaceDocumentFacet Document() const override;
    void ShowDocument(std::string_view key) override;
    bool OpenAsset(const ProjectFile& file) override;
    [[nodiscard]] std::vector<WorkspaceResourceFacet> Resources() override;
    [[nodiscard]] WorkspaceSelectionFacet Selection() override;
    bool RevealProblem(const Problem& problem) override;
    [[nodiscard]] ExplorerTable* Explorers() override { return &ExplorerRows; }
    [[nodiscard]] IWorkspaceTimeline* Timeline() override { return this; }
    [[nodiscard]] WorkspaceTimelineFacet Facet() override;
    void Scrub(double time, TimelineDrag drag) override;
    void MoveMark(std::string_view, double, TimelineDrag) override {}
    void Pick(std::string_view) override {}

    // For tests: the model the workspace presents.
    [[nodiscard]] AbilityWorkbench& Bench() { return *Workbench; }

private:
    // What the card is about.
    enum class CardSubject : std::uint8_t
    {
        Document,
        Creating,
    };

    void DefineActions(ActionCatalog& catalog);
    void DefineExplorers(ActionCatalog& catalog);
    void DefineTransport(ActionCatalog& catalog);
    // The preview shows the ability's working data, replayed when it changes.
    void SyncPreview();
    void PublishProblems();
    void ShowStrip();
    void AddPhase();
    // The palette's entry `entry` made an op where the strip's slot or the selection says.
    void InsertOp(std::string_view entry);
    // A drag let go of, and nothing it carried moved: what a focus loss and a hidden tab mean.
    void Interrupt();
    // The selected element taken out; a phase that transitions go to asks first.
    void DeleteSelected();
    bool OpenDocument(const std::string& path);
    // The card back on the ability, from another document it was showing.
    void ShowAbility();
    // A new effect, asked where, named by the selected op's `argument` and shown.
    void CreateEffect(std::string argument);

    std::unique_ptr<AbilityWorkbench> Workbench;
    std::unique_ptr<DataDocumentSheet> Sheet;
    DataSubtypeAuthoringRegistry Authoring;
    std::unique_ptr<DataProperties> ElementFields;
    std::unique_ptr<AbilityArgumentProperties> Arguments;
    std::unique_ptr<AbilityProblemProperties> ElementProblems;
    std::unique_ptr<AbilityOverviewProperties> Overview;
    std::unique_ptr<AbilityPreviewSession> Preview;
    std::unique_ptr<AbilityPreviewControls> PreviewControls;
    std::unique_ptr<AbilityPreviewInspector> PreviewInspector;
    const DataDocument* Previewed = nullptr;
    std::uint64_t PreviewedRevision = 0;
    std::unique_ptr<DataCreation> Creation;
    std::unique_ptr<AbilityPhaseStrip> Strip;
    std::vector<AbilityPaletteEntry> Palette;
    DocumentSourceSet& Sources;
    ProblemList& Problems;
    PromptQueue::Asker Prompts;
    std::string ProblemSignature;
    ActionContext ActionHandlers;
    ExplorerTable ExplorerRows;
    TreeOutline AssetOutline;
    TreeOutline ElementOutline;
    TreeOutline PaletteOutline;
    CardSubject Subject = CardSubject::Document;
    const ActionContextStack* WindowActions = nullptr;
    Rect2d ContentRect{};
    // What a drag on the strip carries, and the slot an op is wanted in.
    std::string Carried;
    std::string InsertSlot;
    bool Visible = false;
};
