#include <camera/CameraTiltState.h>

#include <camera/CameraTilt.h>
#include <ecs/World.h>
#include <math/Angle.h>
#include <math/Smoothing.h>
#include <world/transform/TransformHistory.h>

#include <algorithm>
#include <cmath>

float CameraTiltRoll(const CameraTilt& tilt, float sideways)
{
    if (!(tilt.FullSpeed > 0.0f) || !std::isfinite(sideways))
        return 0.0f;
    // Negative roll lowers the view's right side: it leans the way it moves.
    return -ToDegrees(tilt.MaxRoll) * std::clamp(sideways / tilt.FullSpeed, -1.0f, 1.0f);
}

void CameraTiltState::Update(const World& world, EntityId camera, const CameraEffectClock& now)
{
    const bool continues = Step.Continues(camera);
    const double seconds = Step.Advance(camera, now);
    PresentedCamera = camera;
    Presented = {};

    const CameraTilt* tilt =
        world.IsRegistered<CameraTilt>() && camera.IsValid() ? world.TryGet<CameraTilt>(camera) : nullptr;
    if (tilt == nullptr)
    {
        Roll = 0.0f;
        return;
    }
    const EntityId subject = CameraViewSubject(world, camera);
    const Vec3d position = PresentationPoseOf(world, subject, now.Alpha).Position;
    const Vec3d right = PresentationPoseOf(world, camera, now.Alpha).Rotation.RotateVector(Vec3d::Right());
    if (!continues)
    {
        Roll = 0.0f;
        LastSubjectPosition = position;
        return;
    }
    if (seconds > 0.0)
    {
        const Vec3d velocity = (position - LastSubjectPosition) / static_cast<float>(seconds);
        const float sideways = velocity.Magnitude() <= kCameraTiltMaxSpeed ? velocity.Dot(right) : 0.0f;
        Roll = ApproachExponentially(Roll, CameraTiltRoll(*tilt, sideways), seconds, tilt->Response);
        LastSubjectPosition = position;
    }
    if (Settings.Scale > 0.0f)
        Presented.Rotation.Z = Roll * Settings.Scale;
}

CameraViewOffset CameraTiltState::OffsetFor(EntityId camera) const
{
    return camera.IsValid() && camera == PresentedCamera ? Presented : CameraViewOffset{};
}
