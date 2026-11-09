#include <camera/CameraEffects.h>

#include <camera/CameraFovScale.h>
#include <camera/CameraShake.h>
#include <ecs/World.h>
#include <time/SimClock.h>
#include <world/transform/TransformHistory.h>

void PresentCameraEffects(World& world, EntityId camera, const FixedSimulationLoop& clock, double presentationAlpha)
{
    const CameraEffectClock now = CameraEffectClock::At(clock, presentationAlpha);
    if (CameraShakeState* shake = world.TryGetResource<CameraShakeState>())
        shake->Update(world, camera, PresentationPoseOf(world, camera, presentationAlpha).Position, now);
    if (CameraFovScaleState* fov = world.TryGetResource<CameraFovScaleState>())
        fov->Update(world, camera, now);
}
