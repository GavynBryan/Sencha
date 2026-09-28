#include "authoring/AnimationPreviewWorkspace.h"

#include "authoring/AnimationEventBindings.h"
#include "authoring/AnimationNameDeclarations.h"
#include "authoring/AnimationNewFiles.h"
#include "data/DataAssetFiles.h"
#include "ui/DataForm.h"

#include <anim/AnimBehaviorSet.h>
#include <anim/AnimFactSchema.h>
#include <anim/AnimBlendOverrides.h>
#include <anim/AnimBlendspaceData.h>
#include <anim/AnimFlowData.h>
#include <anim/AnimRequestSchema.h>
#include <anim/AnimRigData.h>
#include <core/json/JsonParser.h>
#include <gameplay_tags/GameplayTagDeclarations.h>
#include <anim/AnimSelectorData.h>
#include <anim/AnimSlotMapData.h>
#include <anim/AnimationClipSampling.h>
#include <anim/AnimPoseComposition.h>
#include <anim/SkinningPalette.h>
#include <assets/data/DataAssetSubtype.h>
#include <assets/runtime/ContentTagDeclarations.h>
#include "authoring/AnimationClipPlayerMigration.h"
#include <gameplay_tags/GameplayTagRegistry.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>
#include <core/json/JsonFormat.h>
#include <authored/VerbBindingData.h>

#include <algorithm>
#include <sstream>
#include <deque>
#include <span>
#include <array>
#include <format>
#include <filesystem>
#include <fstream>

AnimationPreviewWorkspace::AnimationPreviewWorkspace(RuntimeAssets& assets, std::function<void(World&)> vocabulary,
                                                     std::filesystem::path authoringRoot)
    : Documents(assets, Sources, { .ContentRoot = authoringRoot,
                                   .Subtypes = { AnimationDocumentSubtypes().begin(), AnimationDocumentSubtypes().end() } })
    , ClipEvents(assets, Sources)
    , Audition(assets)
    , Viewport(assets)
    , Rig(assets, Tags.Vocabulary(vocabulary))
    , Vocabulary(Tags.Vocabulary(std::move(vocabulary)))
    , Lab(assets, Vocabulary)
    , ClipPlayers(authoringRoot)
    , Assets(assets)
    , AuthoringRoot(std::move(authoringRoot))
{
    Documents.OnChanged([this](DataDocument& document, bool residentChanged) {
        DataDocumentChanged(document, residentChanged);
    });
    ClipEvents.OnChanged([this](AnimationClipEventsDocument&, bool clipChanged) {
        if (clipChanged)
            Rig.Simulation.Rebind();
    });
    RefreshBrowser();
}

void AnimationPreviewWorkspace::RefreshBrowser()
{
    Tags.Refresh(Assets, Documents);
    Content.Refresh(Assets.Registry, Assets.Assets.DefaultSource());
}

std::span<const std::string_view> AnimationDocumentSubtypes()
{
    static constexpr std::string_view kSubtypes[] = { kAnimRigType, kAnimBehaviorSetType, kAnimSlotMapType,
                                                      kAnimSelectorType, kAnimFlowType, kAnimBlendspaceType,
                                                      kAnimBlendOverridesType, kAnimRequestSchemaType,
                                                      kAnimFactSchemaType, kVerbBindingsTypeName,
                                                      kGameplayTagDeclarationsType };
    return kSubtypes;
}

void AnimationPreviewWorkspace::DataDocumentChanged(DataDocument& document, bool residentChanged)
{
    if (document.Subtype() == kGameplayTagDeclarationsType)
    {
        Tags.Refresh(Assets, Documents);
        Rig.Simulation.VocabularyChanged();
    }
    if (residentChanged)
        Rig.Simulation.Rebind();
}

std::string AnimationPreviewWorkspace::PreviewStatusOf(const DataDocument& document) const
{
    const DataResidentState* state = Documents.ResidentStateOf(document);
    if (state == nullptr)
        return {};
    switch (state->Status)
    {
    case DataResidentStatus::Current:
        return "The preview runs the working version.";
    case DataResidentStatus::Pending:
        return "Not loaded by the open rig; nothing in the preview uses it yet.";
    case DataResidentStatus::KeptLastValid:
        return "The working version was refused (" + state->Error + "); the preview keeps the last valid version.";
    }
    return {};
}

std::string AnimationPreviewWorkspace::PreviewStatusOf(const AnimationClipEventsDocument& document) const
{
    const ClipEventsPreviewState* state = ClipEvents.PreviewStateOf(document);
    if (state == nullptr)
        return {};
    switch (state->Status)
    {
    case ClipEventsPreviewStatus::Current:
        return {};
    case ClipEventsPreviewStatus::ClipNotLoaded:
        return "The clip is not loaded, so the preview cannot play these events.";
    case ClipEventsPreviewStatus::KeptLastValid:
        return "The preview keeps the last valid events: " + state->Problem;
    case ClipEventsPreviewStatus::Refused:
        return "The preview's copy of the clip could not be replaced.";
    }
    return {};
}

bool AnimationPreviewWorkspace::SaveDocument(const DocumentRef& document)
{
    const DocumentSaveResult result = Sources.Save(document);
    switch (result.Status)
    {
    case DocumentSaveStatus::Saved:
    case DocumentSaveStatus::SavedWithProblems:
        DocumentError.clear();
        return true;
    case DocumentSaveStatus::Conflict:
        DocumentError = "The file changed on disk since it was read. Keep yours or take the file's under "
                        "Problems and changes > Changes.";
        return false;
    case DocumentSaveStatus::Failed:
        DocumentError = result.Error;
        return false;
    }
    return false;
}

bool AnimationPreviewWorkspace::OpenRig(const std::string& path)
{
    if (!Rig.Open(path))
        return false;
    Viewport.LayerDisplay = {};
    Viewport.Source = AnimationViewportSource::Simulation;
    const std::string& skeleton = Rig.Data()->SkeletonPath;
    if (!skeleton.empty() && Audition.Session.SkeletonPath() != skeleton && Audition.MeshPath.empty())
        (void)Audition.SelectSkeleton(skeleton);
    if (Documents.PushWaiting() | ClipEvents.PushWaiting())
        Rig.Simulation.Rebind();
    return true;
}

const DataAssetCache& AnimationPreviewWorkspace::DataCache() const
{
    return Assets.DataAssets;
}

bool AnimationPreviewWorkspace::ReloadScenario()
{
    return !Rig.Path.empty() && OpenRig(Rig.Path);
}

const AnimationClipCache& AnimationPreviewWorkspace::Clips() const
{
    return Assets.AnimationClips;
}

const SkeletonCache& AnimationPreviewWorkspace::Skeletons() const
{
    return Assets.Skeletons;
}

bool AnimationPreviewWorkspace::CreateRig(const AnimationRigRecipe& recipe, std::string& error)
{
    if (AuthoringRoot.empty())
    {
        error = "No project content root is open to write the rig into.";
        return false;
    }
    const AnimationRigPlan plan = PlanAnimationRig(recipe, Assets.AnimationClips, &Assets.Skeletons);
    if (!plan.Error.empty())
    {
        error = plan.Error;
        return false;
    }
    if (!WriteAnimationNewDocuments(AuthoringRoot, Assets.Registry, plan.Documents, error)
        || !WriteAnimationNewFile(AuthoringRoot, plan.Scenario, 4, error))
        return false;
    RefreshBrowser();
    if (!OpenRig(plan.RigPath))
    {
        error = Rig.Error;
        return false;
    }
    error.clear();
    return true;
}

bool AnimationPreviewWorkspace::MigrateClipPlayers(std::string& error)
{
    if (AuthoringRoot.empty())
    {
        error = "No project content root is open to migrate.";
        return false;
    }
    ClipPlayers.Scan();
    const AnimationClipPlayerMigrationPlan plan =
        PlanAnimationClipPlayerMigration(AuthoringRoot, ClipPlayers.Uses, Assets.AnimationClips);
    if (!plan.Error.empty())
    {
        error = plan.Error;
        return false;
    }
    if (!WriteAnimationNewDocuments(AuthoringRoot, Assets.Registry, plan.Documents, error))
        return false;
    // Two-space indent, as scenes are written.
    for (const AnimationNewDocument& scene : plan.Scenes)
        if (!WriteAnimationNewFile(AuthoringRoot, scene, 2, error))
            return false;
    RefreshBrowser();
    ClipPlayers.Scan();
    error.clear();
    return true;
}

bool AnimationPreviewWorkspace::AuditionClip(const std::string& path)
{
    Viewport.Source = AnimationViewportSource::Audition;
    return Audition.SelectClip(path);
}

void AnimationPreviewWorkspace::Advance(double wallSeconds)
{
    if (Documents.PushWaiting() | ClipEvents.PushWaiting())
        Rig.Simulation.Rebind();
    Audition.Session.Advance(wallSeconds);
    Rig.Simulation.Advance(wallSeconds);
}

void AnimationPreviewWorkspace::ExtractViewport()
{
    Viewport.Extract(Audition, Rig.Simulation, Rig.Navigation, Takes.A());
}

std::vector<AnimationScenarioRun> AnimationPreviewWorkspace::RunScenarioBatch(bool againstOpenRig)
{
    if (againstOpenRig && Rig.Path.empty())
        return {};
    AnimationPreviewSession batch(Assets.DataAssets, &Assets.AnimationClips, Vocabulary, &Assets.Skeletons);
    return RunAnimationScenarios(batch, Assets.Assets, AuthoringRoot, againstOpenRig ? Rig.Path : std::string());
}
