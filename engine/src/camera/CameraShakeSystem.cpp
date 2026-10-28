#include <camera/CameraShakeSystem.h>

#include <app/GameContexts.h>
#include <camera/CameraShake.h>
#include <components/ActiveCameraService.h>
#include <ecs/World.h>
#include <runtime/RuntimeFrameLoop.h>
#include <world/transform/TransformComponents.h>

void CameraShakeSystem::FrameUpdate(FrameUpdateContext& ctx)
{
    World& world = ctx.Entities;
    CameraShakeState* state = world.TryGetResource<CameraShakeState>();
    const ActiveCameraService* active = world.TryGetResource<ActiveCameraService>();
    if (state == nullptr)
        return;
    const EntityId camera = active != nullptr && world.IsAlive(active->GetActive()) ? active->GetActive() : EntityId{};
    const WorldTransform* pose = camera.IsValid() ? std::as_const(world).TryGet<WorldTransform>(camera) : nullptr;
    const FixedSimulationLoop& clock = ctx.Runtime.GetSimulationClock();
    state->Update(world, camera, pose != nullptr ? pose->Value.Position : Vec3d::Zero(),
                  { .Tick = clock.GetTickIndex(), .Alpha = ctx.Presentation.Alpha, .TickSeconds = clock.GetFixedDt() });
}
