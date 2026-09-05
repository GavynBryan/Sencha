#include "CameraRelativeSteeringSystem.h"

#include "OrbitCameraSystem.h"
#include "PlatformerInputActions.h"

#include <app/GameContexts.h>
#include <ecs/Query.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <input/InputActionSource.h>
#include <input/InputActionState.h>
#include <math/Quat.h>
#include <movement/MovementIntent.h>
#include <movement/MovementTags.h>
#include <world/transform/TransformComponents.h>

#include <cmath>
#include <cstdint>

Vec3d CameraRelativeWish(float cameraYaw, Vec2d move)
{
    const Quatf frame = Quatf::FromAxisAngle(Vec3d::Up(), cameraYaw);
    Vec3d wish = frame.RotateVector(Vec3d::Forward()) * move.Y
               + frame.RotateVector(Vec3d::Right()) * move.X;
    wish.Y = 0.0f;
    const float squared = wish.SqrMagnitude();
    if (squared > 1.0f)
        wish = wish * (1.0f / std::sqrt(squared));
    return wish;
}

float FacingYawFor(const Vec3d& wish, float current)
{
    if (wish.SqrMagnitude() < 1e-6f)
        return current;
    // Forward is -Z in this engine's frame, so a wish along it is yaw zero.
    return std::atan2(-wish.X, -wish.Z);
}

void CameraRelativeSteeringSystem::FixedLogic(FixedLogicContext& ctx)
{
    World& world = ctx.Entities;
    if (!world.IsRegistered<MovementIntent>()
        || !world.IsRegistered<GameplayTagContainer>())
    {
        return;
    }
    const MovementTags* tags = world.TryGetResource<MovementTags>();
    const PlatformerInputActions* actionIds =
        world.TryGetResource<PlatformerInputActions>();
    const OrbitCameraState* orbit = world.TryGetResource<OrbitCameraState>();
    if (tags == nullptr || actionIds == nullptr || orbit == nullptr)
        return;

    const InputActionSources sources(world);
    const float cameraYaw = orbit->Yaw;

    Query<Write<MovementIntent>, Write<LocalTransform>, Read<GameplayTagContainer>>
        query(world);
    query.ForEachChunkIn(ctx.Partitions, [&](auto& view)
    {
        auto intents = view.template Write<MovementIntent>();
        auto transforms = view.template Write<LocalTransform>();
        const auto entityTags = view.template Read<GameplayTagContainer>();
        for (std::uint32_t index = 0; index < view.Count(); ++index)
        {
            if (!entityTags[index].HasExact(tags->Controlled))
                continue;
            const InputActionView input = sources.TickFor(view.Entity(index));
            const Vec3d wish = CameraRelativeWish(cameraYaw, input.Axis2(actionIds->Move));
            intents[index].WishDir = wish;
            intents[index].Jump = input.Fired(actionIds->Jump);

            // The body faces where it runs. Its current yaw is read back from
            // its own rotation, so a body left alone keeps facing where it
            // stopped.
            const Vec3d facing =
                transforms[index].Value.Rotation.RotateVector(Vec3d::Forward());
            const float current = std::atan2(-facing.X, -facing.Z);
            transforms[index].Value.Rotation =
                Quatf::FromAxisAngle(Vec3d::Up(), FacingYawFor(wish, current));
        }
    });
}
