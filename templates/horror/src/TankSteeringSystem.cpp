#include "TankSteeringSystem.h"

#include "HorrorInputActions.h"

#include <app/GameContexts.h>
#include <ecs/Query.h>
#include <ecs/World.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <input/InputActionSource.h>
#include <math/Quat.h>
#include <movement/MovementIntent.h>
#include <movement/MovementTags.h>
#include <world/transform/TransformComponents.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

TankStep TankSteer(float yaw, Vec2d move, float turnRate, float dt)
{
    TankStep step;
    // Pushing right turns clockwise seen from above, which is a negative yaw
    // about the up axis in a right-handed frame.
    step.Yaw = yaw - move.X * turnRate * dt;
    const float walk = std::clamp(move.Y, -1.0f, 1.0f);
    const Quatf facing = Quatf::FromAxisAngle(Vec3d::Up(), step.Yaw);
    step.Wish = facing.RotateVector(Vec3d::Forward()) * walk;
    step.Wish.Y = 0.0f;
    return step;
}

void TankSteeringSystem::FixedLogic(FixedLogicContext& ctx)
{
    World& world = ctx.Entities;
    if (!world.IsRegistered<MovementIntent>()
        || !world.IsRegistered<GameplayTagContainer>())
    {
        return;
    }
    const MovementTags* tags = world.TryGetResource<MovementTags>();
    const HorrorInputActions* actionIds = world.TryGetResource<HorrorInputActions>();
    if (tags == nullptr || actionIds == nullptr)
        return;

    const InputActionSources sources(world);
    const float dt = static_cast<float>(ctx.Time.DeltaSeconds);

    // Cached across ticks: constructing a query scans the archetypes.
    if (!Steer)
        Steer.emplace(world);
    Steer->ForEachChunkIn(ctx.Partitions, [&](auto& view)
    {
        auto intents = view.template Write<MovementIntent>();
        auto transforms = view.template Write<LocalTransform>();
        const auto entityTags = view.template Read<GameplayTagContainer>();
        for (std::uint32_t index = 0; index < view.Count(); ++index)
        {
            if (!entityTags[index].HasExact(tags->Controlled))
                continue;
            const InputActionView input = sources.TickFor(view.Entity(index));
            const Vec3d facing =
                transforms[index].Value.Rotation.RotateVector(Vec3d::Forward());
            const float yaw = std::atan2(-facing.X, -facing.Z);
            const TankStep step = TankSteer(yaw, input.Axis2(actionIds->Move), TurnRate, dt);
            intents[index].WishDir = step.Wish;
            intents[index].Jump = false;
            transforms[index].Value.Rotation = Quatf::FromAxisAngle(Vec3d::Up(), step.Yaw);
        }
    });
}
