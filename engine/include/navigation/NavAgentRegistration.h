#pragma once

#include <app/EngineSchedule.h>
#include <movement/MovementRegistration.h>
#include <navigation/NavAgentSystem.h>

// Registers the navigation agent follower as a movement producer. Opt-in, after
// RegisterMovementSystems; `jobs` may be null, which plans inline.
void RegisterNavigationAgents(EngineSchedule& schedule, RuntimeWorld& runtime, JobSystem* jobs,
                              const DataAssetCache* dataAssets, ConsoleRegistry* console);

// Orders the follower after a game system that issues navigation requests, so a
// request made in tick N is planned in tick N.
template <typename TDecisionSystem>
void OrderNavigationAfter(EngineSchedule& schedule)
{
    schedule.After<NavAgentSystem, TDecisionSystem>();
}
