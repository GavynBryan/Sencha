#include "HearingScan.h"

#include <perception/PerceptionFrame.h>
#include <world/SimulationAuthority.h>

#include <algorithm>

bool StagePerceptionStimulus(World& world, const PerceptionStimulus& stimulus)
{
    if (!IsSimulationAuthority(world) || !(stimulus.Radius >= 0.0f))
        return false;
    if (!world.HasResource<PerceptionStimuli>())
        world.AddResource<PerceptionStimuli>();
    world.GetResource<PerceptionStimuli>().Staged.push_back(stimulus);
    return true;
}

void HearingScan::Run(World& world, const StoragePartitionSet& partitions, GameplayTagId sense, PerceptionFrame& frame,
                      HearingStats& stats)
{
    PerceptionStimuli* stimuli = world.TryGetResource<PerceptionStimuli>();
    Heard.clear();
    if (stimuli != nullptr)
        std::swap(Heard, stimuli->Staged);
    stats.Stimuli = static_cast<std::uint32_t>(Heard.size());
    if (!world.IsRegistered<HearingSensor>())
        return;
    if (QueriedWorld != &world)
    {
        Ears.emplace(world);
        QueriedWorld = &world;
    }

    Placed.clear();
    Ears->ForEachChunkIn(partitions, [this](auto& view) {
        const auto transforms = view.template Read<WorldTransform>();
        const auto ears = view.template Read<HearingSensor>();
        for (std::uint32_t row = 0; row < view.Count(); ++row)
            Placed.push_back({ view.Entity(row), transforms[row].Value.TransformPoint(ears[row].EarOffset),
                               ears[row].Sensitivity });
    });
    std::ranges::sort(Placed, {}, &PlacedEar::Entity);
    stats.Sensors = static_cast<std::uint32_t>(Placed.size());

    for (const PlacedEar& ear : Placed)
    {
        frame.BeginScan(ear.Entity, sense);
        for (const PerceptionStimulus& stimulus : Heard)
        {
            if (stimulus.Source == ear.Entity || (stimulus.Only.IsValid() && stimulus.Only != ear.Entity))
                continue;
            const float reach = stimulus.Radius * ear.Sensitivity;
            const float distance = (stimulus.Position - ear.Ear).Magnitude();
            if (!stimulus.Only.IsValid() && distance > reach)
                continue;
            const float confidence = stimulus.Only.IsValid() || reach <= 0.0f ? 1.0f : 1.0f - distance / reach;
            frame.Add(stimulus.Source, stimulus.Position, confidence);
            ++stats.Heard;
        }
        frame.EndScan(ScanCoverage::Complete);
    }
}
