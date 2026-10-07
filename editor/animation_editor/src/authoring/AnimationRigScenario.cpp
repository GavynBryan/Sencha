#include "authoring/AnimationRigScenario.h"

#include "authoring/AnimationScenario.h"

#include <anim/AnimRigData.h>
#include <assets/runtime/RuntimeAssets.h>
#include <core/assets/AssetRegistry.h>

#include <algorithm>
#include <filesystem>
#include <format>

AnimationRigScenario::AnimationRigScenario(RuntimeAssets& assets, std::function<void(World&)> vocabulary)
    : Simulation(assets.DataAssets, &assets.AnimationClips, std::move(vocabulary), &assets.Skeletons)
    , Assets(assets)
{
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
    ScenarioFile = sidecar.string();
    Error.clear();
    (void)Simulation.Open(std::move(scenario));
    Navigation = AnimationNavigation{};
    return true;
}

bool AnimationRigScenario::Save()
{
    if (!Simulation.IsOpen() || ScenarioFile.empty())
        return false;
    if (!SaveAnimationScenario(Simulation.Scenario(), ScenarioFile, Error))
        return false;
    Simulation.MarkScenarioSaved();
    Error.clear();
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
