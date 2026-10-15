#include "authoring/AnimationPreviewWorkspace.h"

#include "authoring/AnimationEventBindings.h"
#include "authoring/AnimationNameDeclarations.h"
#include "authoring/AnimationNewFiles.h"
#include "data/DataAssetFiles.h"
#include "prompts/DocumentPrompts.h"
#include "text/PathLeaf.h"

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
#include <assets/runtime/ContentVocabulary.h>
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

AnimationPreviewWorkspace::AnimationPreviewWorkspace(RuntimeAssets& assets, DocumentSourceSet& sources,
                                                     DataDocumentStore& store, std::function<void(World&)> vocabulary,
                                                     std::filesystem::path authoringRoot)
    : Sources(sources)
    , Documents(store, { .ContentRoot = authoringRoot,
                         .Subtypes = { AnimationDocumentSubtypes().begin(), AnimationDocumentSubtypes().end() } })
    , ClipEvents(assets, Sources)
    , Audition(assets)
    , Viewport(assets)
    , Rig(assets, Sources, Tags.Vocabulary(vocabulary))
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

bool AnimationPreviewWorkspace::OpenRig(const std::string& path, DirtyDisposition disposition)
{
    if (Rig.Scenario.IsChanged())
    {
        const DocumentRef scenario = Rig.Scenario.Ref();
        if (disposition == DirtyDisposition::Refuse)
        {
            Rig.Error = std::format("{} has unsaved changes.", Rig.Scenario.DocumentLabel(scenario.Key));
            return false;
        }
        if (disposition == DirtyDisposition::Discard)
            Rig.Scenario.DiscardDocument(scenario.Key);
        else if (const DocumentSaveResult saved = Sources.Save(scenario); saved.Status != DocumentSaveStatus::Saved)
        {
            Rig.Error = std::format("{} was not saved: {}", Rig.Scenario.DocumentLabel(scenario.Key), saved.Error);
            return false;
        }
    }
    if (!Rig.Open(path))
        return false;
    Viewport.LayerDisplay = {};
    Viewport.Source = AnimationViewportSource::Simulation;
    const std::string& skeleton = Rig.Data()->SkeletonPath;
    if (!skeleton.empty() && Audition.Session.SkeletonPath() != skeleton && Audition.MeshPath.empty())
        (void)Audition.SelectSkeleton(skeleton);
    if (Documents.Store().PushWaiting() | ClipEvents.PushWaiting())
        Rig.Simulation.Rebind();
    return true;
}

const DataAssetCache& AnimationPreviewWorkspace::DataCache() const
{
    return Assets.DataAssets;
}

void AnimationPreviewWorkspace::AppendDialDocuments(WorkspaceDocumentFacet& facet)
{
    if (Rig.Scenario.IsOpen())
        facet.Open.push_back({ .Key = DialKeyOf(Rig.Scenario.Ref()),
                               .Label = Rig.Scenario.DocumentLabel(Rig.Scenario.Ref().Key),
                               .Changed = Rig.Scenario.IsChanged() });
    for (const std::unique_ptr<AnimationClipEventsDocument>& events : ClipEvents.Documents())
        facet.Open.push_back({ .Key = std::string(kEventsDialKey) + events->ClipPath(),
                               .Label = ClipEvents.DocumentLabel(events->ClipPath()),
                               .Changed = events->IsDirty() });
}

std::string AnimationPreviewWorkspace::DialKeyOf(const DocumentRef& document) const
{
    if (document.Source == &ClipEvents)
        return std::string(kEventsDialKey) + document.Key;
    if (document.Source == &Rig.Scenario)
        return std::string(kScenarioDialKey) + document.Key;
    return document.Key;
}

bool AnimationPreviewWorkspace::RevertScenario()
{
    return Rig.Scenario.Revert(Rig.Error);
}

bool OpenRigAsking(AnimationPreviewWorkspace& workspace, PromptQueue::Asker& asker, const std::string& path,
                   std::function<void()> opened)
{
    if (!workspace.Rig.Scenario.IsChanged())
    {
        const bool done = workspace.OpenRig(path);
        if (done && opened)
            opened();
        return done;
    }
    AskAboutDocuments(asker, "Open " + std::string(PathLeaf(path)), { workspace.Rig.Scenario.Ref() },
                      [&workspace, path, opened = std::move(opened)](DirtyDisposition disposition) {
                          if (workspace.OpenRig(path, disposition) && opened)
                              opened();
                      });
    return true;
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
    if (Documents.Store().PushWaiting() | ClipEvents.PushWaiting())
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
