#include "FreeCamera.h"

#include <math/Quat.h>
#include <world/transform/TransformComponents.h>

#include <SDL3/SDL.h>

void FreeCamera::UpdateLook(const InputFrame& input)
{
    LookHeld = input.IsMouseButtonDown(SDL_BUTTON_RIGHT);
    if (!LookHeld)
        return;

    Yaw -= input.MouseDeltaX * MouseSensitivity;
    Pitch -= input.MouseDeltaY * MouseSensitivity;
    Pitch = SDL_clamp(Pitch, -1.5f, 1.5f);
}

void FreeCamera::TickFixed(const InputFrame& input, World& world, float fixedDt)
{
    LocalTransform* transform = world.TryGet<LocalTransform>(Entity);
    if (transform == nullptr)
        return;

    Vec3d move = Vec3d::Zero();
    if (input.IsKeyDown(SDL_SCANCODE_W)) move += Vec3d::Forward();
    if (input.IsKeyDown(SDL_SCANCODE_S)) move += Vec3d::Backward();
    if (input.IsKeyDown(SDL_SCANCODE_D)) move += Vec3d::Right();
    if (input.IsKeyDown(SDL_SCANCODE_A)) move += Vec3d::Left();
    if (input.IsKeyDown(SDL_SCANCODE_E)) move += Vec3d::Up();
    if (input.IsKeyDown(SDL_SCANCODE_Q)) move += Vec3d::Down();

    ApplyRotation(world);

    if (move.SqrMagnitude() > 0.0f)
    {
        move = move.Normalized();
        const float speed = MoveSpeed
            * (input.IsKeyDown(SDL_SCANCODE_LSHIFT) ? FastMultiplier : 1.0f);
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
