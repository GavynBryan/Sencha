#include <camera/CameraEffects.h>

#include <camera/CameraFovScale.h>
#include <camera/CameraShake.h>
#include <ecs/World.h>
#include <time/SimClock.h>
#include <world/transform/TransformComponents.h>

void PresentCameraEffects(World& world, EntityId camera, const FixedSimulationLoop& clock, double presentationAlpha)
{
    const CameraEffectClock now{ .Tick = clock.GetTickIndex(),
                                 .Alpha = presentationAlpha,
                                 .TickSeconds = clock.GetFixedDt() };
    if (CameraShakeState* shake = world.TryGetResource<CameraShakeState>())
    {
        const WorldTransform* pose = std::as_const(world).TryGet<WorldTransform>(camera);
        shake->Update(world, camera, pose != nullptr ? pose->Value.Position : Vec3d::Zero(), now);
    }
    if (CameraFovScaleState* fov = world.TryGetResource<CameraFovScaleState>())
        fov->Update(world, camera, now);
}
