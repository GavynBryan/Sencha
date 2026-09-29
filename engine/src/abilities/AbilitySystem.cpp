#include <abilities/AbilitySystem.h>

#include <abilities/AbilityActivation.h>
#include <abilities/AbilityAnimation.h>
#include <abilities/AbilityDefinition.h>
#include <abilities/AbilityRegistry.h>
#include <abilities/AbilitySet.h>
#include <attributes/AttributeSet.h>
#include <effects/EffectDefinition.h>
#include <effects/EffectRegistry.h>
#include <effects/EffectSystem.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <anim/AnimRequestJournal.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>

#include <utility>
#include <vector>

namespace
{
bool CanAfford(
    const World& world,
    EntityId actor,
    EffectId cost,
    const EffectRegistry& effects)
{
    if (!cost.IsValid())
        return true;
    const EffectDefinition* def = effects.Get(cost);
    if (def == nullptr)
        return true;
    const AttributeSet* set = world.TryGet<AttributeSet>(actor);
    if (set == nullptr)
        return true;

    for (const EffectModifier& modifier : def->Modifiers)
    {
        if (modifier.Op == ModifierOp::Add && modifier.Magnitude < 0.0f)
            if (set->GetBase(modifier.Attr, 0.0f) < -modifier.Magnitude)
                return false;
    }
    return true;
}

// An activation's Held request is its own: leased to the effect entity that is the
// activation, and cancelled by the kit when that entity ends.
void Lease(World& world, EntityId actor, EntityId activation, AnimRequestId request)
{
    if (!world.HasComponent<AbilityAnimationLeases>(actor))
        world.AddComponent(actor, AbilityAnimationLeases{});
    AbilityAnimationLeases& leases = *world.TryGet<AbilityAnimationLeases>(actor);
    for (AbilityAnimationLeases::Lease& lease : leases.Leases)
    {
        if (lease.Request.IsValid())
            continue;
        lease = AbilityAnimationLeases::Lease{ .Activation = activation, .Request = request };
        return;
    }
}

// The request the activation asks of its actor, through the one door every
// producer uses: issued on the authority, predicted for the pawn a client predicts.
void RequestAbilityAnimation(World& world, EntityId actor, const AbilityAnimation& animation, EntityId activation,
                             std::uint64_t tick)
{
    if (!animation.Intent.IsValid() || !world.IsRegistered<AnimRequestSet>()
        || !world.HasComponent<AnimRequestSet>(actor))
        return;
    AnimRequestDesc desc;
    desc.Source = actor;
    desc.Intent = animation.Intent;
    desc.Layers = animation.Layers;
    desc.Lifetime = animation.Lifetime;
    desc.FixedTicks = animation.FixedTicks;
    const bool held = desc.Lifetime == AnimRequestLifetime::Held;
    if (held && !activation.IsValid())
        desc.Lifetime = AnimRequestLifetime::Impulse;
    if (desc.Lifetime == AnimRequestLifetime::Held)
        desc.Owner = activation;
    const AnimRequestResult result = RequestAnimation(world, actor, desc, tick);
    if (result.Accepted() && desc.Lifetime == AnimRequestLifetime::Held)
        Lease(world, actor, activation, result.Id);
}

void ProcessAbilityActivationsImpl(
    World& world,
    const StoragePartitionSet* partitions,
    std::uint64_t tick)
{
    ReleaseEndedAbilityAnimations(world, tick);

    AbilityActivationQueue* queue =
        world.TryGetResource<AbilityActivationQueue>();
    if (queue == nullptr)
        return;

    std::vector<AbilityActivation> pending = std::move(queue->Pending);
    queue->Pending.clear();

    for (const AbilityActivation& intent : pending)
    {
        if (!world.IsAlive(intent.Actor))
            continue;

        if (partitions != nullptr
            && !partitions->Contains(world.GetEntityPartition(intent.Actor)))
        {
            queue->Pending.push_back(intent);
            continue;
        }

        TryActivateAbility(world, intent.Actor, intent.Ability, tick);
    }
}
} // namespace

bool TryActivateAbility(World& world, EntityId actor, AbilityId ability, std::uint64_t tick)
{
    const AbilityRegistry* abilities =
        std::as_const(world).TryGetResource<AbilityRegistry>();
    if (abilities == nullptr || !world.IsAlive(actor))
        return false;
    const AbilityDefinition* def = abilities->Get(ability);
    if (def == nullptr)
        return false;

    const AbilitySet* owned = std::as_const(world).TryGet<AbilitySet>(actor);
    if (owned == nullptr || !owned->Has(ability))
        return false;

    if (const GameplayTagRegistry* tagReg =
            std::as_const(world).TryGetResource<GameplayTagRegistry>())
    {
        const GameplayTagContainer* tags =
            std::as_const(world).TryGet<GameplayTagContainer>(actor);
        const GameplayTagContainer empty{};
        if (!def->ActivationRequirements.Matches(
                tags != nullptr ? *tags : empty,
                *tagReg))
        {
            return false;
        }
    }

    if (const EffectRegistry* effects =
            std::as_const(world).TryGetResource<EffectRegistry>())
    {
        if (!CanAfford(std::as_const(world), actor, def->Cost, *effects))
            return false;
    }

    if (def->Cost.IsValid())
        ApplyEffect(world, actor, def->Cost);
    if (def->Cooldown.IsValid())
        ApplyEffect(world, actor, def->Cooldown);
    EntityId activation;
    if (def->OnActivate.IsValid())
        activation = ApplyEffect(world, actor, def->OnActivate);
    RequestAbilityAnimation(world, actor, def->Animation, activation, tick);
    return true;
}

void ReleaseEndedAbilityAnimations(World& world, std::uint64_t tick)
{
    if (!world.IsRegistered<AbilityAnimationLeases>())
        return;
    world.ForEachComponent<AbilityAnimationLeases>([&](EntityId actor, AbilityAnimationLeases& leases) {
        for (AbilityAnimationLeases::Lease& lease : leases.Leases)
        {
            if (!lease.Request.IsValid() || world.IsAlive(lease.Activation))
                continue;
            (void)CancelAnimation(world, actor, lease.Request, AnimCancelReason::Released, tick);
            lease = AbilityAnimationLeases::Lease{};
        }
    });
}

void ProcessAbilityActivations(World& world, std::uint64_t tick)
{
    ProcessAbilityActivationsImpl(world, nullptr, tick);
}

void ProcessAbilityActivations(
    World& world,
    const StoragePartitionSet& partitions,
    std::uint64_t tick)
{
    ProcessAbilityActivationsImpl(world, &partitions, tick);
}
