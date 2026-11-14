#include <camera/CameraBobState.h>

#include <camera/CameraBob.h>
#include <components/StrideCycle.h>
#include <ecs/World.h>
#include <math/Smoothing.h>

#include <algorithm>
#include <cmath>
#include <numbers>

CameraViewOffset EvaluateCameraBob(const CameraBob& bob, float cycle, float strength)
{
    if (!(strength > 0.0f) || !std::isfinite(cycle))
        return {};
    const float wave = std::sin(2.0f * std::numbers::pi_v<float> * cycle);
    CameraViewOffset offset;
    // |sin| turns sharply at zero, so each footfall lands as a quick dip and the
    // view floats over the top of the step.
    offset.Translation.Y = -bob.Drop * (1.0f - std::abs(wave)) * strength;
    offset.Translation.X = bob.Sway * wave * strength;
    return offset;
}

void CameraBobState::Update(const World& world, EntityId camera, const CameraEffectClock& now)
{
    const bool continues = Step.Continues(camera);
    const double seconds = Step.Advance(camera, now);
    PresentedCamera = camera;
    Presented = {};

    const CameraBob* bob = world.IsRegistered<CameraBob>() && camera.IsValid() ? world.TryGet<CameraBob>(camera)
                                                                                 : nullptr;
    if (bob == nullptr)
    {
        Strength = 0.0f;
        return;
    }
    const EntityId subject = CameraViewSubject(world, camera);
    const StrideCycle* stride =
        world.IsRegistered<StrideCycle>() && world.IsAlive(subject) ? world.TryGet<StrideCycle>(subject) : nullptr;
    const float alpha = static_cast<float>(std::clamp(now.Alpha, 0.0, 1.0));

    const float speed = stride != nullptr ? StrideGroundSpeedAt(*stride, alpha) : 0.0f;
    const float target = bob->FullSpeed > 0.0f ? std::clamp(speed / bob->FullSpeed, 0.0f, 1.0f) : 0.0f;
    Strength = continues ? ApproachExponentially(Strength, target, seconds, bob->Response) : target;

    if (stride != nullptr && Settings.Scale > 0.0f)
        Presented = EvaluateCameraBob(*bob, StrideCyclePositionAt(*stride, alpha), Strength * Settings.Scale);
}

CameraViewOffset CameraBobState::OffsetFor(EntityId camera) const
{
    return camera.IsValid() && camera == PresentedCamera ? Presented : CameraViewOffset{};
}
