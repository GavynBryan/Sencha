#pragma once

#include <app/GameContexts.h>
#include <ecs/Query.h>
#include <perception/ObservationMemory.h>

#include <optional>

// Folds each tick's PerceptionFrame into every observer's ObservationMemory and
// ages what was not observed. Authority only; runs after PerceptionSystem.
class ObservationMemorySystem
{
public:
    void FixedLogic(FixedLogicContext& ctx);

private:
    const World* QueriedWorld = nullptr;
    std::optional<Query<Write<ObservationMemory>>> Memories;
};
