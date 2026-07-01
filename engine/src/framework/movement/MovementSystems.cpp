#include <framework/movement/MovementSystems.h>

#include <ecs/CommandBuffer.h>
#include <ecs/World.h>
#include <framework/abilities/AbilityActivation.h>
#include <framework/abilities/AbilityDefinition.h>
#include <framework/abilities/AbilityRegistry.h>
#include <framework/abilities/AbilitySet.h>
#include <framework/attributes/AttributeRegistry.h>
#include <framework/attributes/AttributeSet.h>
#include <framework/effects/ActiveEffect.h>
#include <framework/effects/EffectDefinition.h>
#include <framework/effects/EffectRegistry.h>
#include <framework/gameplay_tags/GameplayTagContainer.h>
#include <framework/gameplay_tags/GameplayTagQuery.h>
#include <framework/gameplay_tags/GameplayTagRegistry.h>
#include <framework/movement/MovementDefs.h>
#include <framework/movement/MovementIntent.h>
#include <framework/movement/MovementModes.h>
#include <framework/movement/MovementProfile.h>
#include <framework/movement/MovementState.h>
#include <framework/movement/MovementTags.h>
#include <physics/components/CharacterController.h>

#include <algorithm>
#include <utility>
#include <vector>

namespace
{
    constexpr float kMoveEpsilon = 1e-6f;
    // Fallback top speed when a character carries no MoveSpeed attribute.
    constexpr float kDefaultMoveSpeed = 6.0f;

    Vec3d MoveToward(const Vec3d& current, const Vec3d& target, float maxDelta)
    {
        const Vec3d delta = target - current;
        const float dist = delta.Magnitude();
        if (dist <= maxDelta || dist <= kMoveEpsilon)
            return target;
        return current + delta * (maxDelta / dist);
    }

    // Movement is the single owner of the state tags, so keep each at a stack
    // count of zero or one rather than re-granting (which would stack) every tick.
    void EnsureTag(GameplayTagContainer& tags, GameplayTagId tag, bool want)
    {
        const bool has = tags.HasExact(tag);
        if (want && !has)
            tags.Grant(tag);
        else if (!want && has)
            tags.Revoke(tag);
    }

    // Shared planar steering used by both modes. With input, converge
    // PlanarVelocity toward WishDir * targetSpeed at `accel`. With no input, decay
    // to rest at `friction` (ground); friction 0 preserves momentum (air). Vertical
    // is owned by the mover.
    void SteerPlanar(MovementState& state,
                     const Vec3d& wishDir,
                     float targetSpeed,
                     float accel,
                     float friction,
                     float dt)
    {
        Vec3d wish = wishDir;
        wish.Y = 0.0f;

        if (wish.SqrMagnitude() > kMoveEpsilon)
            state.PlanarVelocity = MoveToward(state.PlanarVelocity, wish * targetSpeed, accel * dt);
        else if (friction > 0.0f)
            state.PlanarVelocity = MoveToward(state.PlanarVelocity, Vec3d::Zero(), friction * dt);

        state.PlanarVelocity.Y = 0.0f;
    }

    float TargetSpeed(const World& world, EntityId entity, const MovementDefs* defs)
    {
        if (defs == nullptr)
            return kDefaultMoveSpeed;
        if (const AttributeSet* attrs = world.TryGet<AttributeSet>(entity))
            return attrs->GetCurrent(defs->MoveSpeed, kDefaultMoveSpeed);
        return kDefaultMoveSpeed;
    }

    // Ground and air share the per-entity plumbing (fetch intent/profile/speed,
    // write DesiredVelocity); they differ only in accel/friction, passed in.
    void StepLocomotion(World& world, float dt, bool ground)
    {
        if (!world.IsRegistered<MovementState>() || !world.IsRegistered<MovementIntent>()
            || !world.IsRegistered<MovementProfile>() || !world.IsRegistered<CharacterController>())
            return;
        if (ground ? !world.IsRegistered<OnGround>() : !world.IsRegistered<InAir>())
            return;

        const MovementDefs* defs = std::as_const(world).TryGetResource<MovementDefs>();

        world.ForEachComponent<MovementState>([&](EntityId entity, MovementState& state)
        {
            const bool inMode = ground ? world.HasComponent<OnGround>(entity)
                                       : world.HasComponent<InAir>(entity);
            if (!inMode)
                return;

            const MovementIntent* intent = std::as_const(world).TryGet<MovementIntent>(entity);
            const MovementProfile* profile = std::as_const(world).TryGet<MovementProfile>(entity);
            CharacterController* controller = world.TryGet<CharacterController>(entity);
            if (intent == nullptr || profile == nullptr || controller == nullptr)
                return;

            const float accel = ground ? profile->Acceleration
                                       : profile->Acceleration * profile->AirControl;
            const float friction = ground ? profile->Friction : 0.0f;
            SteerPlanar(state, intent->WishDir, TargetSpeed(world, entity, defs), accel, friction, dt);

            controller->DesiredVelocity = state.PlanarVelocity;
        });
    }
}

void TickLocomotionTransitions(World& world, float dt)
{
    if (!world.IsRegistered<MovementState>() || !world.IsRegistered<CharacterController>()
        || !world.IsRegistered<OnGround>() || !world.IsRegistered<InAir>())
        return;

    const MovementTags* ids = std::as_const(world).TryGetResource<MovementTags>();

    struct ModeSwap
    {
        EntityId Entity;
        bool ToGround;
    };
    std::vector<ModeSwap> swaps;

    world.ForEachComponent<MovementState>([&](EntityId entity, MovementState& state)
    {
        const CharacterController* controller = std::as_const(world).TryGet<CharacterController>(entity);
        if (controller == nullptr)
            return;
        const bool grounded = controller->Grounded;

        // Coyote grace: full while grounded, decaying once contact is lost.
        const MovementProfile* profile = std::as_const(world).TryGet<MovementProfile>(entity);
        const float coyoteSeconds = profile != nullptr ? profile->CoyoteSeconds : 0.0f;
        if (grounded)
            state.CoyoteTimer = coyoteSeconds;
        else if (state.CoyoteTimer > 0.0f)
            state.CoyoteTimer = std::max(0.0f, state.CoyoteTimer - dt);

        // The physical mode marker follows real contact. Record the edge; the
        // marker swap is a structural change, applied through a CommandBuffer
        // after iteration.
        const bool hasGround = world.HasComponent<OnGround>(entity);
        const bool hasAir = world.HasComponent<InAir>(entity);
        if (grounded && !hasGround)
            swaps.push_back({ entity, true });
        else if (!grounded && !hasAir)
            swaps.push_back({ entity, false });

        // Tag projection. Jump gating reads movement.grounded, so coyote grace
        // keeps that tag alive briefly even as the marker flips to InAir.
        if (ids != nullptr)
        {
            if (GameplayTagContainer* tags = world.TryGet<GameplayTagContainer>(entity))
            {
                const bool coyoteGrounded = grounded || state.CoyoteTimer > 0.0f;
                const MovementIntent* intent = std::as_const(world).TryGet<MovementIntent>(entity);
                const bool moving = intent != nullptr && intent->WishDir.SqrMagnitude() > kMoveEpsilon;
                EnsureTag(*tags, ids->Grounded, coyoteGrounded);
                EnsureTag(*tags, ids->GroundedIdle, coyoteGrounded && !moving);
                EnsureTag(*tags, ids->GroundedWalking, coyoteGrounded && moving);
                EnsureTag(*tags, ids->Airborne, !coyoteGrounded);
            }
        }
    });

    if (swaps.empty())
        return;

    CommandBuffer commands(world);
    for (const ModeSwap& swap : swaps)
    {
        if (swap.ToGround)
        {
            if (world.HasComponent<InAir>(swap.Entity))
                commands.RemoveComponent<InAir>(swap.Entity);
            commands.AddComponent<OnGround>(swap.Entity);
        }
        else
        {
            if (world.HasComponent<OnGround>(swap.Entity))
                commands.RemoveComponent<OnGround>(swap.Entity);
            commands.AddComponent<InAir>(swap.Entity);
        }
    }
    commands.Flush();
}

void TickGroundLocomotion(World& world, float dt)
{
    StepLocomotion(world, dt, /*ground*/ true);
}

void TickAirLocomotion(World& world, float dt)
{
    StepLocomotion(world, dt, /*ground*/ false);
}

void TickJumpExecution(World& world)
{
    if (!world.IsRegistered<GameplayTagContainer>() || !world.IsRegistered<CharacterController>())
        return;
    const MovementTags* ids = std::as_const(world).TryGetResource<MovementTags>();
    if (ids == nullptr)
        return;

    world.ForEachComponent<GameplayTagContainer>([&](EntityId entity, GameplayTagContainer& tags)
    {
        if (!tags.HasExact(ids->JumpRequested))
            return;
        if (CharacterController* controller = world.TryGet<CharacterController>(entity))
        {
            const MovementProfile* profile = std::as_const(world).TryGet<MovementProfile>(entity);
            controller->PendingJumpSpeed = profile != nullptr ? profile->JumpSpeed : 0.0f;
        }
        tags.Revoke(ids->JumpRequested); // single fire; the mover consumes PendingJumpSpeed when grounded
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
    if (!world.IsRegistered<OnGround>())
        world.RegisterComponent<OnGround>();
    if (!world.IsRegistered<InAir>())
        world.RegisterComponent<InAir>();
    if (!world.IsRegistered<GameplayTagContainer>())
        world.RegisterComponent<GameplayTagContainer>();
    if (!world.IsRegistered<AttributeSet>())
        world.RegisterComponent<AttributeSet>();
    if (!world.IsRegistered<AbilitySet>())
        world.RegisterComponent<AbilitySet>();
    if (!world.IsRegistered<ActiveEffect>())
        world.RegisterComponent<ActiveEffect>();

    GameplayTagRegistry& tagReg = world.HasResource<GameplayTagRegistry>()
        ? world.GetResource<GameplayTagRegistry>()
        : world.AddResource<GameplayTagRegistry>();
    AttributeRegistry& attrReg = world.HasResource<AttributeRegistry>()
        ? world.GetResource<AttributeRegistry>()
        : world.AddResource<AttributeRegistry>();
    EffectRegistry& effReg = world.HasResource<EffectRegistry>()
        ? world.GetResource<EffectRegistry>()
        : world.AddResource<EffectRegistry>();
    AbilityRegistry& abilityReg = world.HasResource<AbilityRegistry>()
        ? world.GetResource<AbilityRegistry>()
        : world.AddResource<AbilityRegistry>();
    if (!world.HasResource<AbilityActivationQueue>())
        world.AddResource<AbilityActivationQueue>();

    const MovementTags tags = RegisterMovementTags(tagReg);
    if (!world.HasResource<MovementTags>())
        world.AddResource<MovementTags>(tags);

    MovementDefs defs;
    defs.MoveSpeed = attrReg.RegisterAttribute("MoveSpeed", 0.0f, 100.0f, kDefaultMoveSpeed);

    // Jump is authored data, not a code path: gated by grounded, blocked by its
    // own cooldown tag, and its "behavior" is a short effect that grants the
    // one-tick request tag TickJumpExecution consumes. Cooldown outlives the
    // request so the two grants never overlap.
    EffectDefinition request;
    request.Duration = EffectDuration::Duration;
    request.DurationSeconds = 0.05f;
    request.GrantedTags = { tags.JumpRequested };
    const EffectId requestFx = effReg.Register("movement.jump.request", request);

    EffectDefinition cooldown;
    cooldown.Duration = EffectDuration::Duration;
    cooldown.DurationSeconds = 0.3f;
    cooldown.GrantedTags = { tags.JumpCooldown };
    const EffectId cooldownFx = effReg.Register("movement.jump.cooldown", cooldown);

    AbilityDefinition jump;
    jump.ActivationRequirements
        .AddAll(tags.Grounded, GameplayTagMatchMode::Hierarchical)
        .AddNone(tags.JumpCooldown);
    jump.Cooldown = cooldownFx;
    jump.OnActivate = requestFx;
    defs.Jump = abilityReg.Register("movement.jump", jump);

    if (!world.HasResource<MovementDefs>())
        world.AddResource<MovementDefs>(defs);
}
