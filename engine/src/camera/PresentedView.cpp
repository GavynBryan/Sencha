#include <camera/PresentedView.h>

#include <camera/CameraBobState.h>
#include <camera/CameraFovScale.h>
#include <camera/CameraShake.h>
#include <camera/CameraTiltState.h>
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
    CameraViewOffset offset;
    if (const CameraShakeState* shake = world.TryGetResource<CameraShakeState>())
        offset += shake->OffsetFor(camera);
    if (const CameraBobState* bob = world.TryGetResource<CameraBobState>())
        offset += bob->OffsetFor(camera);
    if (const CameraTiltState* tilt = world.TryGetResource<CameraTiltState>())
        offset += tilt->OffsetFor(camera);
    const CameraFovScaleState* fovScale = world.TryGetResource<CameraFovScaleState>();
    return PresentedView{
        .Pose = ApplyCameraViewOffset(placed, offset),
        .Offset = offset.IsIdentity() ? Transform3f::Identity() : CameraViewOffsetTransform(offset),
        .FovYRadians = std::clamp(component.FovYRadians * (fovScale != nullptr ? fovScale->ScaleFor(camera) : 1.0f),
                                  ToRadians(1.0f), ToRadians(179.0f)),
    };
}
