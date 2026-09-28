#pragma once

#include "authoring/AnimationBlendComparison.h"
#include "authoring/AnimationClipPlayerMigration.h"
#include "authoring/AnimationSessionLab.h"
#include "authoring/AnimationClipEventsSet.h"
#include "authoring/AnimationContentLists.h"
#include "authoring/AnimationContentTags.h"
#include "authoring/AnimationClipPreviewSession.h"
#include "authoring/AnimationRigRecipe.h"
#include "authoring/AnimationScenarioBatch.h"
#include "authoring/AnimationPreviewSession.h"
#include "render/AnimationPreviewScene.h"
#include "data/DataDocumentSet.h"
#include "documents/DocumentSourceSet.h"

#include <anim/AnimPoseEvaluation.h>
#include <anim/Skeleton.h>
#include <anim/SkeletonHandle.h>
#include <core/assets/AssetLease.h>
#include <math/Mat.h>

#include <filesystem>
#include <functional>
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

[[nodiscard]] std::span<const std::string_view> AnimationDocumentSubtypes();

// Owns preview selections and their leases, the open animation documents, the
// rig under simulation, and navigation.
class AnimationPreviewWorkspace final
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
    // Gameplay tags the open rig's content uses that nothing declares.
    [[nodiscard]] std::vector<std::string> UndeclaredNames();
    // One undo step on the tag declarations beside the rig, created if absent.
    bool DeclareUndeclaredNames(std::string& error);
    [[nodiscard]] std::string PreviewStatusOf(const DataDocument& document) const;
    [[nodiscard]] std::string PreviewStatusOf(const AnimationClipEventsDocument& document) const;
    // Leaves a message in DocumentError when the save did not happen.
    bool SaveDocument(const DocumentRef& document);
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

    // Declared before the vocabulary and every session that captures it.
    AnimationContentTags Tags;
    // Declared before the document sets, which register with it.
    DocumentSourceSet Sources;
    DataDocumentSet Documents;
    AnimationClipEventsSet ClipEvents;
    AnimationContentLists Content;
    AnimationNavigation Navigation;
    AnimationViewportSource ViewportSource = AnimationViewportSource::Audition;
    AnimationLayerDisplay LayerDisplay;
    std::optional<AnimationPoseTake> TakeA;
    AnimationPoseComparison Comparison;
    bool ShowGhost = true;
    std::string ViewportNote;
    std::string DocumentError;

    AnimationClipPreviewSession Session;
    AnimationPreviewSession Simulation;
    // The game module's hook, then Tags.
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
    AnimationPreviewScene Scene;
    std::string MeshPath;
    std::string ClipPath;
    std::string MaterialPath;
    std::string Error;

private:
    void DataDocumentChanged(DataDocument& document, bool residentChanged);
    bool SetSkeletonContent(SkeletonHandle skeleton);
    // Refuses before writing anything if any document already exists.
    bool WriteNewDocuments(const std::vector<AnimationNewDocument>& documents, std::string& error);
    bool WriteFile(const AnimationNewDocument& document, int indent, std::string& error);
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
