#include <camera/PresentedView.h>

#include <camera/CameraFovScale.h>
#include <camera/CameraShake.h>
#include <components/CameraComponent.h>
#include <ecs/World.h>
#include <math/Angle.h>

#include <algorithm>

PresentedView PresentCameraView(const World& world,
                                EntityId camera,
                                const CameraComponent& component,
                                const Transform3f& placed)
{
    // Presented effects scale the camera's own field of view, never replace it,
    // and move the view rather than the camera.
    const CameraFovScaleState* fovScale = world.TryGetResource<CameraFovScaleState>();
    const CameraShakeState* shake = world.TryGetResource<CameraShakeState>();
    return PresentedView{
        .Pose = shake != nullptr ? ApplyCameraShake(placed, shake->OffsetFor(camera)) : placed,
        .FovYRadians = std::clamp(component.FovYRadians * (fovScale != nullptr ? fovScale->ScaleFor(camera) : 1.0f),
                                  ToRadians(1.0f), ToRadians(179.0f)),
    };
}
