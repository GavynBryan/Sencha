#include "authoring/AnimationPreviewWorkspace.h"

#include "authoring/AnimationEventBindings.h"
#include "authoring/AnimationNameDeclarations.h"
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
    if (!WriteNewDocuments(plan.Documents, error) || !WriteFile(plan.Scenario, 4, error))
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

bool AnimationPreviewWorkspace::WriteFile(const AnimationNewDocument& document, int indent, std::string& error)
{
    const std::filesystem::path file = AuthoringRoot / document.RelativePath;
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary);
    out << JsonFormat(document.Root, indent) << "\n";
    if (out.good())
        return true;
    error = std::format("Could not write '{}'.", document.RelativePath);
    return false;
}

bool AnimationPreviewWorkspace::WriteNewDocuments(const std::vector<AnimationNewDocument>& documents,
                                                  std::string& error)
{
    for (const AnimationNewDocument& document : documents)
        if (std::filesystem::exists(AuthoringRoot / document.RelativePath)
            || Assets.Registry.Contains("asset://" + document.RelativePath))
        {
            error = std::format("'{}' already exists; choose another name.", document.RelativePath);
            return false;
        }
    for (const AnimationNewDocument& document : documents)
    {
        if (!WriteFile(document, 4, error))
            return false;
        RegisterDataAssetFile(Assets.Registry, "asset://" + document.RelativePath, AuthoringRoot / document.RelativePath);
    }
    return true;
}

void AnimationPreviewWorkspace::ScanClipPlayers()
{
    ClipPlayerUses.clear();
    ClipPlayerProblems.clear();
    if (!AuthoringRoot.empty())
        ClipPlayerUses = FindAnimationClipPlayers(AuthoringRoot, ClipPlayerProblems);
}

bool AnimationPreviewWorkspace::MigrateClipPlayers(std::string& error)
{
    if (AuthoringRoot.empty())
    {
        error = "No project content root is open to migrate.";
        return false;
    }
    ScanClipPlayers();
    const AnimationClipPlayerMigrationPlan plan =
        PlanAnimationClipPlayerMigration(AuthoringRoot, ClipPlayerUses, Assets.AnimationClips);
    if (!plan.Error.empty())
    {
        error = plan.Error;
        return false;
    }
    if (!WriteNewDocuments(plan.Documents, error))
        return false;
    // Two-space indent, as scenes are written.
    for (const AnimationNewDocument& scene : plan.Scenes)
        if (!WriteFile(scene, 2, error))
            return false;
    RefreshBrowser();
    ScanClipPlayers();
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
    Viewport.Extract(Audition, Rig.Simulation, Rig.Navigation, TakeA ? &*TakeA : nullptr);
}

bool AnimationPreviewWorkspace::RecordTakeA()
{
    if (!Rig.Simulation.IsOpen() || Rig.Simulation.History().empty())
        return false;
    TakeA = RecordAnimationPoseTake(Rig.Simulation, "A");
    Comparison = {};
    return !TakeA->Ticks.empty();
}

bool AnimationPreviewWorkspace::ReplayAgainstTakeA()
{
    if (!TakeA || TakeA->Ticks.empty() || !Rig.Simulation.IsOpen())
        return false;
    Rig.Simulation.Pause();
    Rig.Simulation.Restart();
    Rig.Simulation.RunTo(TakeA->Ticks.back());
    Comparison = CompareAnimationPoseTakes(*TakeA, RecordAnimationPoseTake(Rig.Simulation, "B"));
    return Comparison.Refusal.empty();
}

void AnimationPreviewWorkspace::RunScenarioBatch(bool againstOpenRig)
{
    ScenarioRuns.clear();
    if (againstOpenRig && Rig.Path.empty())
        return;
    AnimationPreviewSession batch(Assets.DataAssets, &Assets.AnimationClips, Vocabulary, &Assets.Skeletons);
    for (const std::filesystem::path& file : FindAnimationScenarios(AuthoringRoot))
    {
        std::vector<AnimDiagnostic> problems;
        std::optional<AnimationScenario> scenario = LoadAnimationScenario(file.string(), problems);
        AnimationScenarioRun run;
        if (scenario)
        {
            if (againstOpenRig)
                scenario->RigPath = Rig.Path;
            const AssetLease rig = Assets.Assets.LoadLease(scenario->RigPath, AssetType::Data);
            run = RunAnimationScenario(batch, std::move(*scenario), std::move(problems));
        }
        else
        {
            run.Problems = std::move(problems);
        }
        run.File = std::filesystem::relative(file, AuthoringRoot).generic_string();
        ScenarioRuns.push_back(std::move(run));
    }
}

bool AnimationPreviewWorkspace::RunLab()
{
    if (!Rig.Simulation.IsOpen())
        return false;
    if (Lab == nullptr)
        Lab = std::make_unique<AnimationSessionLab>(Assets.DataAssets, &Assets.AnimationClips, Vocabulary,
                                                    &Assets.Skeletons);
    if (!Lab->Open(Rig.Simulation.Scenario(), LabSettings, LabInjections))
        return false;
    Lab->RunTo(LabTick);
    return true;
}

void AnimationPreviewWorkspace::ClearTakeA()
{
    TakeA.reset();
    Comparison = {};
}
