#include <navigation/NavAgentRegistration.h>

#include "NavAgentConsole.h"

#include <world/RuntimeWorld.h>

#include <cassert>

void RegisterNavigationAgents(EngineSchedule& schedule, RuntimeWorld& runtime, JobSystem* jobs,
                              const DataAssetCache* dataAssets, ConsoleRegistry* console)
{
    assert(schedule.Get<FreeLocomotionSystem>() != nullptr
           && "RegisterNavigationAgents needs RegisterMovementSystems first");
    schedule.Register<NavAgentSystem>(NavAgentSystem::Services{ runtime, jobs, dataAssets, console });
    OrderMovementAfterInput<NavAgentSystem>(schedule);
    if (console != nullptr)
        RegisterNavigationAgentConsole(*console, runtime.Entities());
}
