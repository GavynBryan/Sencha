#pragma once

#include "authoring/AnimationBlendComparison.h"
#include "authoring/AnimationClipEventsDocument.h"
#include "authoring/AnimationClipPreviewSession.h"
#include "authoring/AnimationRigRecipe.h"
#include "authoring/AnimationPreviewSession.h"
#include "render/AnimationPreviewScene.h"
#include "data/DataDocument.h"

#include <anim/AnimPoseEvaluation.h>
#include <anim/Skeleton.h>
#include <anim/SkeletonHandle.h>
#include <core/assets/AssetLease.h>
#include <math/Mat.h>

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

struct RuntimeAssets;

// Which of the two previews the viewport shows: content picked by hand, or
// the simulated rig's layers composed.
enum class AnimationViewportSource : std::uint8_t
{
    Audition,
    Simulation,
};

// Which of the simulated rig's layers the viewport composes. Preview-only: it
// never reaches the rig, the simulation or its events. With any layer soloed
// only soloed layers show; a muted layer never does.
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

// The pose the viewport shows of a posed entity: the pose pass's composed
// pose, or -- with a layer muted or soloed -- the layers it posed composed
// again without the hidden ones. Neither touches what the pass keeps.
void AnimationPreviewDisplayPose(const AnimPoseSources& sources, const AnimPosePool::Slot& slot,
                                 const AnimPoseState& state, const AnimSelectorState* selection,
                                 const AnimationLayerDisplay& display, AnimTick tick, double tickSeconds,
                                 AnimPoseScratch& scratch, std::vector<Transform3f>& out);

// What the author is looking at across panels: a rule, the behavior it
// selects, the slot row that resolves it, the content that row plays, and
// optionally a recorded tick instead of the live one. Navigating changes this
// and nothing else -- the simulation, scenario and camera are untouched.
struct AnimationNavigation
{
    std::size_t Layer = 0;
    int Rule = -1;
    GameplayTagId Behavior;
    int Row = -1;
    int Content = -1;
    // A joint of the viewport's skeleton, picked in the viewport or the
    // skeleton tree.
    int Joint = -1;
    // Index into the simulation's history, or none for the live tick.
    std::optional<std::size_t> InspectRecord;
};

// Owns preview selections and their leases, the open animation documents, the
// rig under simulation, and navigation. Browsing and selecting preview content
// is transient and never edits the asset being inspected.
class AnimationPreviewWorkspace
{
public:
    // `vocabulary` installs the project's names -- tags, verbs -- into each
    // preview World; the application passes its loaded module's hook.
    // `authoringRoot` is the content root new assets are written into; empty
    // makes this a workspace that creates nothing.
    explicit AnimationPreviewWorkspace(RuntimeAssets& assets, std::function<void(World&)> vocabulary = {},
                                       std::filesystem::path authoringRoot = {});
    void RefreshBrowser();
    bool SelectMesh(const std::string& path);
    bool SelectSkeleton(const std::string& path);
    bool SelectClip(const std::string& path);
    bool SelectMaterial(const std::string& path);
    void Frame(double wallSeconds);
    // Opens any animation data asset for editing: rig, fact or request schema,
    // behavior set, selector, slot map, flow.
    bool OpenAnimationDocument(const std::string& path);
    // After any change to a document -- an edit committed, an undo -- revalidate
    // it and, when it is valid, apply it to the preview's copy of the asset.
    // The preview keeps its last valid version while the document is invalid.
    void DocumentChanged(DataDocument& document);
    // Commits the document's open edit, then treats it as changed.
    void CommitDocumentEdit(DataDocument& document);
    [[nodiscard]] DataDocument* ActiveDocumentOf(std::string_view subtype);
    [[nodiscard]] DataDocument* FindDocument(std::string_view path);

    // Opens the events of a clip cooked from a mesh source, from that source's
    // import sidecar. Already open: selects it.
    bool OpenClipEvents(const std::string& clipPath);
    [[nodiscard]] AnimationClipEventsDocument* FindClipEvents(std::string_view clipPath);
    // After any change to an events document -- an edit, a preview, an undo.
    // Valid working events replace the preview's copy of the clip, and every
    // rig playing it rebinds; invalid ones leave the preview on the last
    // valid events, and PreviewStatus says why.
    void ClipEventsChanged(AnimationClipEventsDocument& document);
    bool SaveClipEvents(AnimationClipEventsDocument& document);
    // Adds a binding for `verb` under `key` to an authored.bindings document,
    // every argument fed by an input of its own name, as one undo step. The
    // document opens if it is not open.
    bool CreateBinding(const std::string& bindingsPath, const std::string& key, const std::string& verb);

    // Opens a rig under the scenario saved beside it, or a new one-participant
    // scenario when there is none. Keeps the rig and its dependencies resident
    // while it is open.
    bool OpenRig(const std::string& path);
    // Writes the working scenario to its sidecar. Explicit: nothing else does.
    bool SaveScenario();
    // Writes a new rig from `recipe` into the authoring root -- its behavior
    // set, slot map, request schema, the rig and a scenario -- registers it
    // and opens it. Refuses to overwrite; `error` says why it did not.
    bool CreateRig(const AnimationRigRecipe& recipe, std::string& error);

    // A/B of blends: take A is the simulation's pose on every kept tick under
    // the working scenario. Replaying runs the scenario again from tick 0 to
    // A's last tick -- the same inputs, clock, seed and start pose, with
    // whatever has been edited since -- and compares it to A.
    bool RecordTakeA();
    bool ReplayAgainstTakeA();
    void ClearTakeA();
    // The tick the viewport shows: the inspected record's, else the latest.
    [[nodiscard]] std::optional<AnimTick> ShownTick() const;
    // Discards the working scenario for the saved one.
    bool ReloadScenario();
    [[nodiscard]] const DataAssetCache& DataCache() const;
    // The editor's own clips, including working events not yet saved.
    [[nodiscard]] const AnimationClipCache& Clips() const;
    // The skeleton the simulated rig names, when it names one that is loaded.
    [[nodiscard]] const SkeletonData* RigSkeleton() const;
    // One undo step on the open rig's document, opening it first: `edit`
    // changes a copy of the root and returns whether it changed anything.
    bool EditRig(const std::function<bool(JsonValue&)>& edit);
    // Model-space transforms of the joints as the viewport drew them last
    // frame, one per joint of the skeleton on screen.
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
    // Per document path: whether the preview runs its working version, or its
    // last valid one and why.
    std::map<std::string, std::string> PreviewStatus;
    AnimationNavigation Navigation;
    AnimationViewportSource ViewportSource = AnimationViewportSource::Audition;
    AnimationLayerDisplay LayerDisplay;
    std::optional<AnimationPoseTake> TakeA;
    AnimationPoseComparison Comparison;
    // Draws take A's pose on the shown tick over the simulation's.
    bool ShowGhost = true;
    std::string ViewportNote;
    std::vector<std::unique_ptr<DataDocument>> Documents;
    std::size_t ActiveDocument = 0;
    std::vector<std::unique_ptr<AnimationClipEventsDocument>> ClipEventDocuments;
    // The events document the event panels act on; its path.
    std::string ActiveClipEvents;
    std::string DocumentError;

    AnimationClipPreviewSession Session;
    // The rig under its scenario. Separate from content audition: sampling a
    // clip never advances or alters the simulation.
    AnimationPreviewSession Simulation;
    std::string RigPath;
    std::string ScenarioFile;
    std::string ScenarioError;
    std::vector<AnimDiagnostic> ScenarioLoadProblems;
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
    bool SetSkeletonContent(SkeletonHandle skeleton);
    [[nodiscard]] bool ApplyDocumentToPreview(DataDocument& document, std::string& status);
    // The simulation's composed pose, or the audition's.
    [[nodiscard]] const std::vector<Mat4>& ViewportPalette();
    AnimPoseScratch DisplayScratch;
    // Take A's pose on the shown tick, when the ghost is drawn.
    [[nodiscard]] const std::vector<Mat4>* GhostPalette();
    std::vector<Mat4> GhostModel;
    std::vector<Mat4> GhostPaletteScratch;
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
