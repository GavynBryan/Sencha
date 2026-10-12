#include "authoring/AnimationRigScenario.h"

#include "authoring/AnimationScenario.h"

#include <anim/AnimRigData.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>

#include <algorithm>
#include <filesystem>
#include <format>

AnimationRigScenario::AnimationRigScenario(RuntimeAssets& assets, DocumentSourceSet& sources,
                                           std::function<void(World&)> vocabulary)
    : Simulation(assets.DataAssets, &assets.AnimationClips, std::move(vocabulary), &assets.Skeletons)
    , Scenario(sources,
               { .Write =
                     [this](std::string& text, std::string&) {
                         text = AnimationScenarioText(Simulation.Scenario());
                         return true;
                     },
                 .Read =
                     [this](std::string_view text, std::string& error) {
                         std::vector<AnimDiagnostic> problems;
                         std::optional<AnimationScenario> read =
                             ParseAnimationScenarioText(text, Scenario.Path().generic_string(), problems);
                         if (!read)
                         {
                             error = problems.empty() ? std::string("Not a scenario.") : problems.front().Message;
                             return false;
                         }
                         Simulation.ReplaceScenario(std::move(*read));
                         return true;
                     } })
    , Assets(assets)
{
    Simulation.OnScenarioChanged([this] { Scenario.Changed(); });
}

bool AnimationRigScenario::Open(const std::string& path)
{
    const auto* record = Assets.Registry.FindByPath(path);
    AssetLease lease = Assets.Assets.LoadLease(path, AssetType::Data);
    const DataAssetHandle handle = lease ? DataAssetHandle::FromToken(lease.OpaqueToken()) : DataAssetHandle{};
    if (record == nullptr || Assets.DataAssets.TryGet<AnimRigData>(handle, kAnimRigType) == nullptr)
    {
        const std::span<const AssetSystem::LoadFailure> failures = Assets.Assets.LoadFailures();
        const auto failure = std::ranges::find(failures, path, &AssetSystem::LoadFailure::Path);
        Error = failure != failures.end() ? std::format("{} did not load: {}", path, failure->Reason)
                                          : std::format("{} is not an animation.rig asset that loads.", path);
        return false;
    }

    std::filesystem::path sidecar(record->FilePath);
    sidecar.replace_extension(".sanimscenario");
    AnimationScenario scenario;
    LoadProblems.clear();
    if (std::filesystem::exists(sidecar))
    {
        std::optional<AnimationScenario> loaded = LoadAnimationScenario(sidecar.string(), LoadProblems);
        if (!loaded)
        {
            Error = "The saved scenario could not be read; see Problems.";
            return false;
        }
        scenario = std::move(*loaded);
        scenario.RigPath = path;
        scenario.RigId = record->Id;
    }
    else
    {
        scenario.Name = sidecar.stem().string();
        scenario.RigPath = path;
        scenario.RigId = record->Id;
        scenario.Participants = { "player" };
    }

    Simulation.Close();
    Lease = std::move(lease);
    Path = path;
    Error.clear();
    (void)Simulation.Open(std::move(scenario));
    Scenario.Open(std::move(sidecar));
    Navigation = AnimationNavigation{};
    return true;
}

const AnimRigData* AnimationRigScenario::Data() const
{
    return Lease ? Assets.DataAssets.TryGet<AnimRigData>(DataAssetHandle::FromToken(Lease.OpaqueToken()), kAnimRigType)
                 : nullptr;
}

const SkeletonData* AnimationRigScenario::Skeleton() const
{
    const AnimBoundRig* rig = Simulation.Rig();
    return rig != nullptr && rig->Skeleton.IsValid() ? Assets.Skeletons.Get(rig->Skeleton) : nullptr;
}

const AnimationPreviewTickRecord* AnimationRigScenario::Inspected() const
{
    const std::deque<AnimationPreviewTickRecord>& history = Simulation.History();
    if (history.empty())
        return nullptr;
    if (Navigation.InspectRecord && *Navigation.InspectRecord < history.size())
        return &history[*Navigation.InspectRecord];
    return &history.back();
}
