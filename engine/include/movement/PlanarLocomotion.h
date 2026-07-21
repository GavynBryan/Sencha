#pragma once

#include <app/GameContexts.h>
#include <attributes/AttributeSet.h>
#include <ecs/Query.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <movement/MovementDefs.h>
#include <movement/MovementIntent.h>
#include <movement/MovementProfile.h>
#include <movement/MovementState.h>
#include <movement/MovementStep.h>
#include <physics/components/CharacterController.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>

namespace movement
{
inline constexpr float kDefaultMoveSpeed = 6.0f;

inline AttributeId MoveSpeedId(const World& world)
{
    if (const MovementDefs* defs = world.TryGetResource<MovementDefs>())
        return defs->MoveSpeed;
    return AttributeId{};
}

template <typename Policy>
void StepPlanarLocomotion(
    MovementState& state,
    const MovementIntent& intent,
    const MovementProfile& profile,
    float topSpeed,
    CharacterController& controller,
    float dt)
{
    Vec3d wish = intent.WishDir;
    wish.Y = 0.0f;
    const float magnitude = wish.Magnitude();
    const Vec3d wishDir = magnitude > kEpsilon
        ? wish * (1.0f / magnitude)
        : Vec3d::Zero();
    const float wishSpeed = topSpeed * std::min(magnitude, 1.0f);

    Vec3d velocity = state.PlanarVelocity;
    Policy::PreAccelerate(velocity, profile, dt);
    if (magnitude > kEpsilon)
    {
        Accelerate(
            velocity,
            wishDir,
            Policy::WishSpeed(profile, wishSpeed),
            Policy::Acceleration(profile),
            dt);
    }
    velocity.Y = 0.0f;

    state.PlanarVelocity = velocity;
    controller.DesiredVelocity = velocity;
}

template <typename Marker, typename Policy>
class PlanarLocomotionSystem
{
public:
    void FixedLogic(FixedLogicContext& ctx)
    {
        Step(
            ctx.Entities,
            static_cast<float>(ctx.Time.DeltaSeconds),
            &ctx.Partitions);
    }

    void Step(World& world, float dt)
    {
        Step(world, dt, nullptr);
    }

private:
    void Step(
        World& world,
        float dt,
        const StoragePartitionSet* partitions)
    {
        if (!world.IsRegistered<MovementState>()
            || !world.IsRegistered<MovementIntent>()
            || !world.IsRegistered<MovementProfile>()
            || !world.IsRegistered<AttributeSet>()
            || !world.IsRegistered<CharacterController>()
            || !world.IsRegistered<Marker>())
        {
            return;
        }

        if (LastWorld != &world)
        {
            CachedQuery.emplace(world);
            LastWorld = &world;
        }
        const AttributeId moveSpeed = MoveSpeedId(std::as_const(world));

        const auto visit = [&](auto& view)
        {
            auto states = view.template Write<MovementState>();
            const auto intents = view.template Read<MovementIntent>();
            const auto profiles = view.template Read<MovementProfile>();
            const auto attrs = view.template Read<AttributeSet>();
            auto controllers = view.template Write<CharacterController>();
            for (std::uint32_t i = 0; i < view.Count(); ++i)
            {
                const float topSpeed = attrs[i].GetCurrent(
                    moveSpeed,
                    kDefaultMoveSpeed);
                StepPlanarLocomotion<Policy>(
                    states[i],
                    intents[i],
                    profiles[i],
                    topSpeed,
                    controllers[i],
                    dt);
            }
        };

        if (partitions != nullptr)
            CachedQuery->ForEachChunkIn(*partitions, visit);
        else
            CachedQuery->ForEachChunk(visit);
    }

    const World* LastWorld = nullptr;
    std::optional<Query<
        Write<MovementState>,
        Read<MovementIntent>,
        Read<MovementProfile>,
        Read<AttributeSet>,
        Write<CharacterController>,
        With<Marker>>> CachedQuery;
};
} // namespace movement
