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
    , Simulation(assets.DataAssets, &assets.AnimationClips, Tags.Vocabulary(vocabulary), &assets.Skeletons)
    , Vocabulary(Tags.Vocabulary(std::move(vocabulary)))
    , Assets(assets)
    , AuthoringRoot(std::move(authoringRoot))
{
    Documents.OnChanged([this](DataDocument& document, bool residentChanged) {
        DataDocumentChanged(document, residentChanged);
    });
    ClipEvents.OnChanged([this](AnimationClipEventsDocument&, bool clipChanged) {
        if (clipChanged)
            Simulation.Rebind();
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

std::vector<std::string> AnimationPreviewWorkspace::UndeclaredNames()
{
    const GameplayTagRegistry* tags = Simulation.Tags();
    if (tags == nullptr)
        return {};
    std::deque<JsonValue> read;
    return UndeclaredAnimationNames(Simulation.Problems(), *tags, [&](std::string_view path) -> AnimationDocumentView {
        const JsonValue* root = nullptr;
        if (std::optional<JsonValue> current = Documents.CurrentRoot(path))
            root = &read.emplace_back(std::move(*current));
        const JsonValue* type = root != nullptr ? root->Find("type") : nullptr;
        const DataSchema* schema =
            type != nullptr && type->IsString() ? Assets.DataSchemas.Find(type->AsString()) : nullptr;
        return { root, schema != nullptr ? &schema->Root : nullptr };
    });
}

bool AnimationPreviewWorkspace::DeclareUndeclaredNames(std::string& error)
{
    const std::vector<std::string> names = UndeclaredNames();
    if (names.empty() || RigPath.empty())
        return true;
    std::string relative = RigPath.substr(std::string_view("asset://").size());
    const std::size_t suffix = relative.ends_with(".rig.sdata") ? relative.size() - std::string_view(".rig.sdata").size()
                                                                : relative.size() - std::string_view(".sdata").size();
    relative = relative.substr(0, suffix) + ".tags.sdata";
    const std::string path = "asset://" + relative;

    const std::size_t active = Documents.ActiveIndex();
    DataDocument* document = Documents.Find(path);
    if (document == nullptr)
        document = Assets.Registry.Contains(path) ? Documents.OpenOrFocus(path, error)
                                                  : Documents.Create(kGameplayTagDeclarationsType, relative, error);
    if (document == nullptr)
        return false;
    JsonValue root = document->CopyRoot();
    if (AddAnimationTagDeclarations(root, names))
        ApplyFieldEdit(*document, Documents, FieldEdit::Instant(), std::move(root));
    Documents.SetActive(active);
    return true;
}

void AnimationPreviewWorkspace::DataDocumentChanged(DataDocument& document, bool residentChanged)
{
    if (document.Subtype() == kGameplayTagDeclarationsType)
    {
        Tags.Refresh(Assets, Documents);
        Simulation.VocabularyChanged();
    }
    if (residentChanged)
        Simulation.Rebind();
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
    const auto* record = Assets.Registry.FindByPath(path);
    AssetLease lease = Assets.Assets.LoadLease(path, AssetType::Data);
    const DataAssetHandle handle =
        lease ? DataAssetHandle::FromToken(lease.OpaqueToken()) : DataAssetHandle{};
    const AnimRigData* rig = Assets.DataAssets.TryGet<AnimRigData>(handle, kAnimRigType);
    if (record == nullptr || rig == nullptr)
    {
        ScenarioError = "Select an animation.rig asset that loads.";
        return false;
    }
    Viewport.LayerDisplay = {};

    std::filesystem::path sidecar(record->FilePath);
    sidecar.replace_extension(".sanimscenario");

    AnimationScenario scenario;
    ScenarioLoadProblems.clear();
    if (std::filesystem::exists(sidecar))
    {
        std::optional<AnimationScenario> loaded =
            LoadAnimationScenario(sidecar.string(), ScenarioLoadProblems);
        if (!loaded)
        {
            ScenarioError = "The saved scenario could not be read; see Problems.";
            return false;
        }
        scenario = std::move(*loaded);
        scenario.RigPath = path;
    }
    else
    {
        scenario.Name = sidecar.stem().string();
        scenario.RigPath = path;
        scenario.Participants = { "player" };
    }

    Simulation.Close();
    RigLease = std::move(lease);
    RigPath = path;
    ScenarioFile = sidecar.string();
    ScenarioError.clear();
    (void)Simulation.Open(std::move(scenario));
    Viewport.Source = AnimationViewportSource::Simulation;
    Navigation = AnimationNavigation{};
    if (!rig->SkeletonPath.empty() && Audition.Session.SkeletonPath() != rig->SkeletonPath && Audition.MeshPath.empty())
        (void)Audition.SelectSkeleton(rig->SkeletonPath);
    if (Documents.PushWaiting() | ClipEvents.PushWaiting())
        Simulation.Rebind();
    return true;
}

bool AnimationPreviewWorkspace::SaveScenario()
{
    if (!Simulation.IsOpen() || ScenarioFile.empty())
        return false;
    if (!SaveAnimationScenario(Simulation.Scenario(), ScenarioFile, ScenarioError))
        return false;
    Simulation.MarkScenarioSaved();
    ScenarioError.clear();
    return true;
}

const DataAssetCache& AnimationPreviewWorkspace::DataCache() const
{
    return Assets.DataAssets;
}

bool AnimationPreviewWorkspace::ReloadScenario()
{
    return !RigPath.empty() && OpenRig(RigPath);
}

const AnimationClipCache& AnimationPreviewWorkspace::Clips() const
{
    return Assets.AnimationClips;
}

const SkeletonCache& AnimationPreviewWorkspace::Skeletons() const
{
    return Assets.Skeletons;
}

bool AnimationPreviewWorkspace::CreateBinding(const std::string& bindingsPath, const std::string& key,
                                              const std::string& verb)
{
    const VerbRegistry* verbs = Simulation.Verbs();
    const VerbDefinition* definition = verbs != nullptr ? verbs->Get(verbs->Find(verb)) : nullptr;
    if (definition == nullptr)
    {
        DocumentError = std::format("'{}' is not a verb the preview declares.", verb);
        return false;
    }
    DataDocument* document = Documents.OpenOrFocus(bindingsPath, DocumentError);
    if (document == nullptr)
        return false;
    JsonValue root = document->CopyRoot();
    if (!AddAnimationBindingRecord(root, MakeAnimationBindingRecord(key, *definition)))
    {
        DocumentError = std::format("'{}' already declares a binding '{}'.", bindingsPath, key);
        return false;
    }
    document->ReplaceRoot(std::move(root));
    Documents.Changed(*document);
    return true;
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
        error = ScenarioError;
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
        Simulation.Rebind();
    Audition.Session.Advance(wallSeconds);
    Simulation.Advance(wallSeconds);
}

void AnimationPreviewWorkspace::ExtractViewport()
{
    Viewport.Extract(Audition, Simulation, Navigation, TakeA ? &*TakeA : nullptr);
}

bool AnimationPreviewWorkspace::RecordTakeA()
{
    if (!Simulation.IsOpen() || Simulation.History().empty())
        return false;
    TakeA = RecordAnimationPoseTake(Simulation, "A");
    Comparison = {};
    return !TakeA->Ticks.empty();
}

bool AnimationPreviewWorkspace::ReplayAgainstTakeA()
{
    if (!TakeA || TakeA->Ticks.empty() || !Simulation.IsOpen())
        return false;
    Simulation.Pause();
    Simulation.Restart();
    Simulation.RunTo(TakeA->Ticks.back());
    Comparison = CompareAnimationPoseTakes(*TakeA, RecordAnimationPoseTake(Simulation, "B"));
    return Comparison.Refusal.empty();
}

void AnimationPreviewWorkspace::RunScenarioBatch(bool againstOpenRig)
{
    ScenarioRuns.clear();
    if (againstOpenRig && RigPath.empty())
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
                scenario->RigPath = RigPath;
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
    if (!Simulation.IsOpen())
        return false;
    if (Lab == nullptr)
        Lab = std::make_unique<AnimationSessionLab>(Assets.DataAssets, &Assets.AnimationClips, Vocabulary,
                                                    &Assets.Skeletons);
    if (!Lab->Open(Simulation.Scenario(), LabSettings, LabInjections))
        return false;
    Lab->RunTo(LabTick);
    return true;
}

void AnimationPreviewWorkspace::ClearTakeA()
{
    TakeA.reset();
    Comparison = {};
}

bool AnimationPreviewWorkspace::EditRig(const std::function<bool(JsonValue&)>& edit)
{
    DataDocument* rig = Documents.Find(RigPath);
    if (rig == nullptr)
        rig = Documents.OpenOrFocus(RigPath, DocumentError);
    if (rig == nullptr)
        return false;
    JsonValue root = rig->CopyRoot();
    if (!edit(root))
        return false;
    rig->BeginEdit();
    rig->PreviewRoot(std::move(root));
    Documents.CommitEdit(*rig);
    return true;
}

const SkeletonData* AnimationPreviewWorkspace::RigSkeleton() const
{
    const AnimBoundRig* rig = Simulation.Rig();
    return rig != nullptr && rig->Skeleton.IsValid() ? Assets.Skeletons.Get(rig->Skeleton) : nullptr;
}
