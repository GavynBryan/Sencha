#include "FreeCamera.h"

#include <assets/data/DataAssetCache.h>
#include <input/InputProfileData.h>
#include <math/Quat.h>
#include <world/transform/TransformComponents.h>

#include <algorithm>
#include <memory>

namespace
{
constexpr std::string_view kFlyActionSetPath = "asset://host/fly_camera_actions";
constexpr std::string_view kFlyProfilePath = "asset://host/fly_camera_profile";

InputBinding Direct(std::string_view control, float scale = 1.0f, float deadZone = 0.0f)
{
    InputBinding binding;
    binding.Kind = InputBindingKind::Direct;
    binding.Controls[kBindingNegativeX] = *ParseInputControl(control);
    binding.Scale = scale;
    binding.DeadZone = deadZone;
    return binding;
}

AuthoredInputBinding Bind(std::string_view action, InputBinding binding)
{
    return AuthoredInputBinding{ std::string(action), binding };
}

AuthoredInputBinding Cardinal(std::string_view action,
                              std::string_view left,
                              std::string_view right,
                              std::string_view down,
                              std::string_view up)
{
    InputBinding binding;
    binding.Kind = InputBindingKind::Cardinal;
    binding.Controls[kBindingNegativeX] = *ParseInputControl(left);
    binding.Controls[kBindingPositiveX] = *ParseInputControl(right);
    binding.Controls[kBindingNegativeY] = *ParseInputControl(down);
    binding.Controls[kBindingPositiveY] = *ParseInputControl(up);
    return AuthoredInputBinding{ std::string(action), binding };
}

AuthoredInputBinding AxisPair(std::string_view action,
                              std::string_view negative,
                              std::string_view positive)
{
    InputBinding binding;
    binding.Kind = InputBindingKind::AxisPair;
    binding.Controls[kBindingNegativeX] = *ParseInputControl(negative);
    binding.Controls[kBindingPositiveX] = *ParseInputControl(positive);
    return AuthoredInputBinding{ std::string(action), binding };
}
}

InputProfileHandle RegisterFlyCameraInput(DataAssetCache& dataAssets)
{
    auto actions = std::make_shared<CompiledInputActionSet>();
    actions->Actions = {
        { "fly_look", InputActionType::Axis2D, InputActionScope::Presentation },
        { "fly_move", InputActionType::Axis2D, InputActionScope::Simulation },
        { "fly_vertical", InputActionType::Axis1D, InputActionScope::Simulation },
        { "fly_fast", InputActionType::Digital, InputActionScope::Simulation },
        { "fly_look_enable", InputActionType::Digital, InputActionScope::Presentation },
        { "fly_dump_trace", InputActionType::Digital, InputActionScope::Presentation },
    };

    auto profile = std::make_shared<CompiledInputProfile>();
    profile->ActionSetPath = std::string(kFlyActionSetPath);

    AuthoredInputContext fly;
    fly.Name = "fly";
    fly.Priority = 0;
    fly.Bindings = {
        // Raw device displacement: the camera applies its own live-tunable
        // sensitivity on top, which is what the debug UI edits.
        Bind("fly_look", Direct("mouse.delta")),
        Bind("fly_look_enable", Direct("mouse.right")),
        Cardinal("fly_move", "key.a", "key.d", "key.s", "key.w"),
        AxisPair("fly_vertical", "key.q", "key.e"),
        Bind("fly_fast", Direct("key.left_shift")),
        Bind("fly_move", Direct("gamepad.left_stick", 1.0f, 0.2f)),
        Bind("fly_fast", Direct("gamepad.left_shoulder")),
        Bind("fly_dump_trace", Direct("key.f2")),
    };
    // The stick pushes negative when pushed forward; the camera's plane wants
    // forward positive.
    for (AuthoredInputBinding& binding : fly.Bindings)
    {
        if (binding.Binding.Controls[kBindingNegativeX].Source == InputControlSource::GamepadStick)
            binding.Binding.InvertY = true;
    }
    profile->Contexts.push_back(std::move(fly));

    (void)dataAssets.Register(kFlyActionSetPath, std::string(kInputActionSetTypeName), actions);
    return InputProfileHandle{ dataAssets.Register(
        kFlyProfilePath, std::string(kInputProfileTypeName), profile) };
}

void FreeCamera::UpdateLook(const InputActionView& input)
{
    LookHeld = input.Held(Actions.LookEnable);
    if (!LookHeld)
        return;

    const Vec2d look = input.Axis2(Actions.Look);
    Yaw -= look.X * MouseSensitivity;
    Pitch -= look.Y * MouseSensitivity;
    Pitch = std::clamp(Pitch, -1.5f, 1.5f);
}

void FreeCamera::TickFixed(const InputActionView& input, World& world, float fixedDt)
{
    LocalTransform* transform = world.TryGet<LocalTransform>(Entity);
    if (transform == nullptr)
        return;

    const Vec2d planar = input.Axis2(Actions.Move);
    Vec3d move = Vec3d::Forward() * planar.Y
        + Vec3d::Right() * planar.X
        + Vec3d::Up() * input.Axis(Actions.Vertical);

    ApplyRotation(world);

    if (move.SqrMagnitude() > 0.0f)
    {
        move = move.Normalized();
        const float speed = MoveSpeed * (input.Held(Actions.Fast) ? FastMultiplier : 1.0f);
        transform->Value.Position += transform->Value.Rotation.RotateVector(move) * (speed * fixedDt);
    }
}

void FreeCamera::ApplyRotation(World& world) const
{
    LocalTransform* transform = world.TryGet<LocalTransform>(Entity);
    if (transform == nullptr)
        return;

    transform->Value.Rotation =
        Quatf::FromAxisAngle(Vec3d::Up(), Yaw)
        * Quatf::FromAxisAngle(Vec3d::Right(), Pitch);
}
