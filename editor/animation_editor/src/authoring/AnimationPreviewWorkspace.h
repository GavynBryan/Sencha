#pragma once

#include "authoring/AnimationClipEventsDocument.h"
#include "authoring/AnimationClipPreviewSession.h"
#include "authoring/AnimationPreviewSession.h"
#include "render/AnimationPreviewScene.h"
#include "data/DataDocument.h"

#include <anim/AnimPoseComposition.h>
#include <anim/Skeleton.h>
#include <anim/SkeletonHandle.h>
#include <core/assets/AssetLease.h>
#include <math/Mat.h>

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

// The simulated layers as the viewport composes them: each layer's playing
// clip at its content time and current weight, over its mask, in rig order.
// A layer the display hides, or whose clip animates another skeleton than
// `skeletonPath`, contributes nothing; `note` names what plays, or why a
// layer was left out.
[[nodiscard]] std::vector<AnimPoseLayer> AnimationPreviewPoseLayers(const AnimBoundRig& rig,
                                                                    const AnimContentState& content,
                                                                    const AnimSelectorState* selection,
                                                                    const AnimationClipCache& clips,
                                                                    const AnimationLayerDisplay& display,
                                                                    std::string_view skeletonPath, std::string& note);

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
    explicit AnimationPreviewWorkspace(RuntimeAssets& assets, std::function<void(World&)> vocabulary = {});
    void RefreshBrowser();
    bool SelectMesh(const std::string& path);
    bool SelectSkeleton(const std::string& path);
    bool SelectClip(const std::string& path);
    bool SelectMaterial(const std::string& path);
    void Frame(double wallSeconds);
    // Opens any animation data asset for editing: rig, fact or request schema,
    // behavior set, selector, slot map.
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
    // Discards the working scenario for the saved one.
    bool ReloadScenario();
    [[nodiscard]] const DataAssetCache& DataCache() const;
    // The editor's own clips, including working events not yet saved.
    [[nodiscard]] const AnimationClipCache& Clips() const;
    // The skeleton the simulated rig names, when it names one that is loaded.
    [[nodiscard]] const SkeletonData* RigSkeleton() const;
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
    std::vector<std::string> FactSchemaPaths;
    // Per document path: whether the preview runs its working version, or its
    // last valid one and why.
    std::map<std::string, std::string> PreviewStatus;
    AnimationNavigation Navigation;
    AnimationViewportSource ViewportSource = AnimationViewportSource::Audition;
    AnimationLayerDisplay LayerDisplay;
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
    AnimPoseScratch PoseScratch;
    std::vector<Transform3f> SimulationLocal;
    std::vector<Mat4> SimulationModel;
    std::vector<Mat4> SimulationPalette;
    RuntimeAssets& Assets;
    AssetLease RigLease;
    AssetLease MeshLease;
    AssetLease SkeletonLease;
    AssetLease ClipLease;
    AssetLease MaterialLease;
    AssetLease DefaultMaterialLease;
    MaterialHandle DefaultMaterial;
};
