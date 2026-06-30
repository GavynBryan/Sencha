#include <framework/movement/MovementSystems.h>

#include <ecs/World.h>
#include <framework/gameplay_tags/GameplayTagContainer.h>
#include <framework/gameplay_tags/GameplayTagRegistry.h>
#include <framework/movement/MovementIntent.h>
#include <framework/movement/MovementProfile.h>
#include <framework/movement/MovementState.h>
#include <framework/movement/MovementTags.h>

#include <utility>

namespace
{
    constexpr float kMoveEpsilon = 1e-6f;

    Vec3d MoveToward(const Vec3d& current, const Vec3d& target, float maxDelta)
    {
        const Vec3d delta = target - current;
        const float dist = delta.Magnitude();
        if (dist <= maxDelta || dist <= kMoveEpsilon)
            return target;
        return current + delta * (maxDelta / dist);
    }

    // Movement is the single source of these state tags, so keep each at a stack
    // count of zero or one rather than re-granting (which would stack) every tick.
    void EnsureTag(GameplayTagContainer& tags, GameplayTagId tag, bool want)
    {
        const bool has = tags.HasExact(tag);
        if (want && !has)
            tags.Grant(tag);
        else if (!want && has)
            tags.Revoke(tag);
    }
}

void StepMovement(MovementState& state,
                  MovementIntent& intent,
                  const MovementProfile& profile,
                  float dt)
{
    if (state.Grounded)
        state.CoyoteTimer = profile.CoyoteSeconds;
    else if (state.CoyoteTimer > 0.0f)
        state.CoyoteTimer -= dt;

    Vec3d wish = intent.WishDir;
    wish.Y = 0.0f;
    const float speed = profile.MaxSpeed * (intent.Sprint ? profile.SprintMultiplier : 1.0f);
    const Vec3d target = wish * speed;

    const float accel = state.Grounded ? profile.Acceleration : profile.Acceleration * profile.AirControl;
    state.PlanarVelocity = MoveToward(state.PlanarVelocity, target, accel * dt);

    if (state.Grounded && wish.SqrMagnitude() <= kMoveEpsilon)
        state.PlanarVelocity = MoveToward(state.PlanarVelocity, Vec3d::Zero(), profile.Friction * dt);

    state.PlanarVelocity.Y = 0.0f;

    state.JumpRequest = 0.0f;
    if (intent.JumpQueued && (state.Grounded || state.CoyoteTimer > 0.0f))
    {
        state.JumpRequest = profile.JumpUpSpeed;
        state.CoyoteTimer = 0.0f;
    }
    intent.JumpQueued = false;
}

void ResolveMovementTags(GameplayTagContainer& tags,
                         const MovementTags& ids,
                         bool grounded,
                         const MovementIntent& intent)
{
    const bool moving = intent.WishDir.SqrMagnitude() > kMoveEpsilon;
    const bool sprint = intent.Sprint;

    EnsureTag(tags, ids.Grounded, grounded);
    EnsureTag(tags, ids.Airborne, !grounded);
    EnsureTag(tags, ids.GroundedIdle,      grounded && !moving);
    EnsureTag(tags, ids.GroundedWalking,   grounded && moving && !sprint);
    EnsureTag(tags, ids.GroundedSprinting, grounded && moving && sprint);
}

void TickMovement(World& world, float dt)
{
    if (!world.IsRegistered<MovementState>())
        return;
    const MovementTags* ids = std::as_const(world).TryGetResource<MovementTags>();

    world.ForEachComponent<MovementState>([&](EntityId entity, MovementState& state)
    {
        MovementIntent* intent = world.TryGet<MovementIntent>(entity);
        const MovementProfile* profile = world.TryGet<MovementProfile>(entity);
        if (intent == nullptr || profile == nullptr)
            return;

        StepMovement(state, *intent, *profile, dt);

        if (ids != nullptr)
            if (GameplayTagContainer* tags = world.TryGet<GameplayTagContainer>(entity))
                ResolveMovementTags(*tags, *ids, state.Grounded, *intent);
    });
}

void InitializeMovementRegistry(World& world)
{
    if (!world.IsRegistered<MovementIntent>())
        world.RegisterComponent<MovementIntent>();
    if (!world.IsRegistered<MovementState>())
        world.RegisterComponent<MovementState>();
    if (!world.IsRegistered<MovementProfile>())
        world.RegisterComponent<MovementProfile>();
    if (!world.IsRegistered<GameplayTagContainer>())
        world.RegisterComponent<GameplayTagContainer>();

    GameplayTagRegistry& registry = world.HasResource<GameplayTagRegistry>()
        ? world.GetResource<GameplayTagRegistry>()
        : world.AddResource<GameplayTagRegistry>();
    const MovementTags tags = RegisterMovementTags(registry);
    if (!world.HasResource<MovementTags>())
        world.AddResource<MovementTags>(tags);
}
