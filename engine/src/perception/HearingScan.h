#pragma once

#include <ecs/Query.h>
#include <gameplay_tags/GameplayTagId.h>
#include <perception/HearingSensor.h>
#include <perception/PerceptionStimuli.h>
#include <world/transform/TransformComponents.h>

#include <optional>
#include <vector>

class PerceptionFrame;
class StoragePartitionSet;

struct HearingStats
{
    std::uint32_t Sensors = 0;
    std::uint32_t Stimuli = 0;
    std::uint32_t Heard = 0;
};

// Every HearingSensor against the stimuli staged since the last scan, in
// ascending observer order. Owner thread, FixedLogic only.
class HearingScan
{
public:
    void Run(World& world, const StoragePartitionSet& partitions, GameplayTagId sense, PerceptionFrame& frame,
             HearingStats& stats);

private:
    struct PlacedEar
    {
        EntityId Entity;
        Vec3d Ear;
        float Sensitivity = 1.0f;
    };

    const World* QueriedWorld = nullptr;
    std::optional<Query<Read<WorldTransform>, Read<HearingSensor>>> Ears;
    std::vector<PlacedEar> Placed;
    std::vector<PerceptionStimulus> Heard;
};
