#include <camera/CameraFovScaleSystem.h>

#include <app/GameContexts.h>
#include <camera/CameraFovScale.h>
#include <components/ActiveCameraService.h>
#include <ecs/World.h>
#include <runtime/RuntimeFrameLoop.h>

void CameraFovScaleSystem::FrameUpdate(FrameUpdateContext& ctx)
{
    World& world = ctx.Entities;
    CameraFovScaleState* state = world.TryGetResource<CameraFovScaleState>();
    const ActiveCameraService* active = world.TryGetResource<ActiveCameraService>();
    if (state == nullptr)
        return;
    const EntityId camera = active != nullptr && world.IsAlive(active->GetActive()) ? active->GetActive() : EntityId{};
    const FixedSimulationLoop& clock = ctx.Runtime.GetSimulationClock();
    state->Update(world, camera,
                  { .Tick = clock.GetTickIndex(), .Alpha = ctx.Presentation.Alpha, .TickSeconds = clock.GetFixedDt() });
}
