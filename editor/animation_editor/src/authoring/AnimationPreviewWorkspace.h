#pragma once

#include "authoring/AnimationBlendComparison.h"
#include "authoring/AnimationClipPlayerMigration.h"
#include "authoring/AnimationSessionLab.h"
#include "authoring/AnimationClipEventsDocument.h"
#include "authoring/AnimationClipPreviewSession.h"
#include "authoring/AnimationRigRecipe.h"
#include "authoring/AnimationScenarioBatch.h"
#include "authoring/AnimationPreviewSession.h"
#include "render/AnimationPreviewScene.h"
#include "commands/CommandStack.h"
#include "data/DataDocument.h"
#include "ui/DataForm.h"

#include <anim/AnimPoseEvaluation.h>
#include <anim/Skeleton.h>
#include <anim/SkeletonHandle.h>
#include <core/assets/AssetLease.h>
#include <math/Mat.h>

#include <filesystem>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <vector>

struct RuntimeAssets;

enum class AnimationViewportSource : std::uint8_t
{
    Audition,
    Simulation,
};

// Viewport-only layer visibility; never reaches the rig or the simulation.
struct AnimationLayerDisplay
{
    std::uint8_t Muted = 0;
    std::uint8_t Soloed = 0;

    [[nodiscard]] bool Shows(std::size_t layer) const
    {
        const auto bit = static_cast<std::uint8_t>(1u << layer);
        return (Muted & bit) == 0 && (Soloed == 0 || (Soloed & bit) != 0);
    }
};

// With every layer shown this is the pose pass's own result; otherwise the
// shown layers are recomposed into `out` without touching the pass's state.
void AnimationPreviewDisplayPose(const AnimPoseSources& sources, const AnimPosePool::Slot& slot,
                                 const AnimPoseState& state, const AnimSelectorState* selection,
                                 const AnimationLayerDisplay& display, AnimTick tick, double tickSeconds,
                                 AnimPoseScratch& scratch, std::vector<Transform3f>& out);

// Cross-panel selection. Changing it never touches the simulation or scenario.
struct AnimationNavigation
{
    std::size_t Layer = 0;
    int Rule = -1;
    GameplayTagId Behavior;
    int Row = -1;
    int Content = -1;
    int Joint = -1;
    // Index into the simulation's history; empty for the live tick.
    std::optional<std::size_t> InspectRecord;
};

// Paths are asset paths, or clip paths for a clip's events.
struct AnimationSaveReport
{
    std::vector<std::string> Saved;
    // Saved, but with problems a game would refuse to load.
    std::vector<std::string> SavedWithProblems;
    // Changed on disk since read; left for SaveOverFile or AdoptFileVersion.
    std::vector<std::string> Conflicts;
    std::vector<std::pair<std::string, std::string>> Failed;
};

[[nodiscard]] std::span<const std::string_view> AnimationDocumentSubtypes();

// Owns preview selections and their leases, the open animation documents, the
// rig under simulation, and navigation.
class AnimationPreviewWorkspace final : public DataFormHost
{
public:
    // An empty `authoringRoot` makes a workspace that creates no assets.
    explicit AnimationPreviewWorkspace(RuntimeAssets& assets, std::function<void(World&)> vocabulary = {},
                                       std::filesystem::path authoringRoot = {});
    void RefreshBrowser();
    bool SelectMesh(const std::string& path);
    bool SelectSkeleton(const std::string& path);
    bool SelectClip(const std::string& path);
    bool SelectMaterial(const std::string& path);
    void Frame(double wallSeconds);
    bool OpenAnimationDocument(const std::string& path);
    // Written with its schema's required members, then registered and opened.
    bool CreateDocument(std::string_view subtype, std::string relativePath, std::string& error);
    // Gameplay tags the open rig's content uses that nothing declares.
    [[nodiscard]] std::vector<std::string> UndeclaredNames();
    // One undo step on the tag declarations beside the rig, created if absent.
    bool DeclareUndeclaredNames(std::string& error);
    // Revalidates; a valid document replaces the preview's copy of the asset,
    // an invalid one leaves the preview on its last valid version.
    void DocumentChanged(DataDocument& document);
    void CommitDocumentEdit(DataDocument& document);
    [[nodiscard]] DataDocument* ActiveDocumentOf(std::string_view subtype);
    [[nodiscard]] DataDocument* ActiveDocumentAny();
    [[nodiscard]] const DataSchema* SchemaOf(const DataDocument& document) const;

    [[nodiscard]] std::vector<std::string> DataAssetPaths(std::string_view subtype) override;
    void OpenDataAsset(std::string_view path) override;
    void SelectField(const DataFieldSchema& field, std::string_view path) override;
    void EditPreviewed(DataDocument& document) override;
    void EditCommitted(DataDocument& document) override;
    [[nodiscard]] DataDocument* FindDocument(std::string_view path);

    // Selects the document instead when it is already open.
    bool OpenClipEvents(const std::string& clipPath);
    [[nodiscard]] AnimationClipEventsDocument* FindClipEvents(std::string_view clipPath);
    // Same contract as DocumentChanged; every rig playing the clip rebinds.
    void ClipEventsChanged(AnimationClipEventsDocument& document);
    bool SaveClipEvents(AnimationClipEventsDocument& document);
    // One undo step; each verb argument is fed by an input of its own name.
    bool CreateBinding(const std::string& bindingsPath, const std::string& key, const std::string& verb);

    // Uses the scenario saved beside the rig, or a new one-participant scenario.
    // The rig and its dependencies stay resident while it is open.
    bool OpenRig(const std::string& path);
    bool SaveScenario();
    // Refuses before writing anything if any of the rig's files already exists.
    bool CreateRig(const AnimationRigRecipe& recipe, std::string& error);
    void ScanClipPlayers();
    bool MigrateClipPlayers(std::string& error);

    // Replay runs the working scenario from tick 0 to take A's last tick and
    // compares poses, so any edit since the recording shows as a residual.
    bool RecordTakeA();
    bool ReplayAgainstTakeA();
    void ClearTakeA();
    // Always from tick 0, so the result reflects the current edits.
    bool RunLab();
    // The inspected record's tick, else the latest.
    [[nodiscard]] std::optional<AnimTick> ShownTick() const;
    bool ReloadScenario();
    // Documents whose files changed on disk are held back as conflicts; one
    // refusal does not stop the others.
    AnimationSaveReport SaveAll();
    // Settle a conflict: write the working version over the file, or take the
    // file's version as one undo step.
    bool SaveOverFile(std::string_view path, std::string& error);
    bool AdoptFileVersion(std::string_view path, std::string& error);
    // Newest step across every open document, cancelling any open interaction first.
    void Undo();
    void Redo();
    [[nodiscard]] bool CanUndo() const { return Journal.CanUndo(); }
    [[nodiscard]] bool CanRedo() const { return Journal.CanRedo(); }
    // Runs in its own session; the working simulation is untouched.
    void RunScenarioBatch(bool againstOpenRig);
    [[nodiscard]] const DataAssetCache& DataCache() const;
    // Includes working clip events not yet saved.
    [[nodiscard]] const AnimationClipCache& Clips() const;
    [[nodiscard]] const SkeletonCache& Skeletons() const;
    [[nodiscard]] const SkeletonData* RigSkeleton() const;
    // One undo step on the rig document; `edit` returns whether it changed anything.
    bool EditRig(const std::function<bool(JsonValue&)>& edit);
    // One model-space transform per joint, as drawn last frame.
    [[nodiscard]] const std::vector<Mat4>& ViewportModel() const { return ViewportModelTransforms; }
    void SelectDocument(std::size_t index);
    void CancelAuthoringEdit();
    void ValidateDocument(DataDocument& document);
    bool SaveDocument(DataDocument& document);
    bool ReloadDocument(DataDocument& document);

    std::vector<std::string> RequestSchemaPaths;
    std::vector<std::string> RigPaths;
    std::vector<std::string> SelectorPaths;
    std::vector<std::string> BehaviorSetPaths;
    std::vector<std::string> SlotMapPaths;
    std::vector<std::string> FlowPaths;
    std::vector<std::string> BlendspacePaths;
    std::vector<std::string> BlendOverridePaths;
    std::vector<std::string> FactSchemaPaths;
    // Per document path: whether the preview runs the working or last valid version.
    std::map<std::string, std::string> PreviewStatus;
    AnimationNavigation Navigation;
    AnimationViewportSource ViewportSource = AnimationViewportSource::Audition;
    AnimationLayerDisplay LayerDisplay;
    std::optional<AnimationPoseTake> TakeA;
    AnimationPoseComparison Comparison;
    bool ShowGhost = true;
    std::string ViewportNote;
    std::vector<std::unique_ptr<DataDocument>> Documents;
    std::size_t ActiveDocument = 0;
    std::vector<std::unique_ptr<AnimationClipEventsDocument>> ClipEventDocuments;
    // One entry per step any document took, in order.
    CommandStack Journal;
    // Clip path of the events document the event panels act on.
    std::string ActiveClipEvents;
    std::string DocumentError;

    AnimationClipPreviewSession Session;
    AnimationPreviewSession Simulation;
    // Gathered on browser refresh from the project's tag declarations.
    std::vector<std::string> ContentTags;
    std::vector<std::string> ContentTagErrors;
    // The game module's hook, then ContentTags.
    std::function<void(World&)> Vocabulary;
    std::unique_ptr<AnimationSessionLab> Lab;
    std::vector<AnimationClipPlayerUse> ClipPlayerUses;
    std::vector<std::string> ClipPlayerProblems;
    AnimationLabSettings LabSettings;
    std::vector<AnimationLabInjection> LabInjections;
    AnimTick LabTick = 300;
    std::string RigPath;
    std::string ScenarioFile;
    std::string ScenarioError;
    std::vector<AnimDiagnostic> ScenarioLoadProblems;
    std::vector<AnimationScenarioRun> ScenarioRuns;
    AnimationSaveReport LastSave;
    // Documents edited before the preview loaded their asset.
    std::vector<std::string> PendingPreviewDocuments;
    AnimationPreviewScene Scene;
    std::vector<std::string> MeshPaths;
    std::vector<std::string> SkeletonPaths;
    std::vector<std::string> ClipPaths;
    std::vector<std::string> MaterialPaths;
    std::string MeshPath;
    std::string ClipPath;
    std::string MaterialPath;
    std::string Error;

private:
    void RegisterDataFile(const std::string& relativePath);
    class JournalStep;
    void RecordJournalStep(DataDocument& document);
    void RecordJournalStep(AnimationClipEventsDocument& document);
    void CancelOpenEdits();
    void StepDocument(std::string_view path, bool clipEvents, bool undo);
    void RefreshContentTags();
    void ApplyPendingPreviewDocuments();
    bool SetSkeletonContent(SkeletonHandle skeleton);
    // Refuses before writing anything if any document already exists.
    bool WriteNewDocuments(const std::vector<AnimationNewDocument>& documents, std::string& error);
    bool WriteFile(const AnimationNewDocument& document, int indent, std::string& error);
    [[nodiscard]] bool ApplyDocumentToPreview(DataDocument& document, std::string& status);
    [[nodiscard]] const std::vector<Mat4>& ViewportPalette();
    AnimPoseScratch DisplayScratch;
    // Null when the ghost is not drawn.
    [[nodiscard]] const std::vector<Mat4>* GhostPalette();
    std::vector<Mat4> GhostModel;
    std::vector<Mat4> GhostPaletteScratch;
    // The shown palette placed at a moving character's transform.
    std::vector<Mat4> PlacedPalette;
    MaterialHandle GhostMaterial;
    AssetLease GhostMaterialLease;
    std::vector<Transform3f> SimulationLocal;
    std::vector<Mat4> SimulationModel;
    std::vector<Mat4> SimulationPalette;
    std::vector<Mat4> ViewportModelTransforms;
    RuntimeAssets& Assets;
    std::filesystem::path AuthoringRoot;
    AssetLease RigLease;
    AssetLease MeshLease;
    AssetLease SkeletonLease;
    AssetLease ClipLease;
    AssetLease MaterialLease;
    AssetLease DefaultMaterialLease;
    MaterialHandle DefaultMaterial;
};
