#pragma once

#include <app/GameContexts.h>
#include <ecs/Query.h>
#include <gameplay_tags/GameplayTagId.h>
#include <perception/Observation.h>
#include <perception/ObservationMemory.h>

#include <optional>
#include <vector>

// Folds each tick's PerceptionFrame into every observer's ObservationMemory,
// with the facing of each subject as it was observed, ages what was not
// observed, and passes observations between members of an ObservationSharing
// group. Authority only; runs after PerceptionSystem.
class ObservationMemorySystem
{
public:
    void FixedLogic(FixedLogicContext& ctx);

private:
    // An observation on its way to the rest of its group.
    struct SharedObservation
    {
        GameplayTagId Group;
        EntityId From;
        Vec3d FromPosition = Vec3d::Zero();
        float Radius = 0.0f;
        double Due = 0.0;
        Observation Seen;
        Vec3d Facing = Vec3d::Zero();
    };

    void Share(World& world, const FixedLogicContext& ctx);

    const World* QueriedWorld = nullptr;
    std::optional<Query<Write<ObservationMemory>>> Memories;
    double Now = 0.0;
    std::vector<SharedObservation> InFlight;
    std::vector<SharedObservation> Arrived;
};
