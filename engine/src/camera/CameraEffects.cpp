#include <camera/CameraEffects.h>

#include <camera/CameraFovScale.h>
#include <camera/CameraShake.h>
#include <ecs/World.h>
#include <time/SimClock.h>
#include <world/transform/TransformHistory.h>

void PresentCameraEffects(World& world, EntityId camera, const FixedSimulationLoop& clock, double presentationAlpha)
{
    const CameraEffectClock now{ .Tick = clock.GetTickIndex(),
                                 .Alpha = presentationAlpha,
                                 .TickSeconds = clock.GetFixedDt() };
    // Falloff measures from where the camera is drawn, which presentation
    // propagation has not written yet when this runs ahead of extraction.
    if (CameraShakeState* shake = world.TryGetResource<CameraShakeState>())
        shake->Update(world, camera, PresentationPoseOf(world, camera, presentationAlpha).Position, now);
    if (CameraFovScaleState* fov = world.TryGetResource<CameraFovScaleState>())
        fov->Update(world, camera, now);
}
