#pragma once

#include <app/GameContexts.h>
#include <ecs/Query.h>
#include <gameplay_tags/GameplayTagId.h>
#include <perception/Observation.h>
#include <perception/ObservationMemory.h>

#include <cstdint>
#include <optional>
#include <vector>

class GameplayTagRegistry;

// Whether a subject carries a memory's preferred tag, which it forgets last. Answered
// once a tick per subject: a full memory asks for each entry against each observation.
class ObservationPreference
{
public:
    void BeginTick();
    [[nodiscard]] bool Holds(const World& world, const GameplayTagRegistry* tags, GameplayTagId prefer, EntityId subject);

private:
    struct Answer
    {
        std::uint64_t Stamp = 0;
        std::uint32_t Generation = 0;
        GameplayTagId Prefer;
        bool Holds = false;
    };

    std::uint64_t Stamp = 0;
    std::vector<Answer> Answers;
};

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
    ObservationPreference Preferences;
    std::vector<SharedObservation> InFlight;
    std::vector<SharedObservation> Arrived;
};
