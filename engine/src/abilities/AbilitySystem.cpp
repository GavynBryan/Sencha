#include <abilities/AbilitySystem.h>

#include <abilities/AbilityActivation.h>
#include <abilities/AbilityDefinition.h>
#include <abilities/AbilityRegistry.h>
#include <abilities/AbilitySet.h>
#include <attributes/AttributeSet.h>
#include <effects/EffectDefinition.h>
#include <effects/EffectRegistry.h>
#include <effects/EffectSystem.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>

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

void ProcessAbilityActivationsImpl(
    World& world,
    const StoragePartitionSet* partitions)
{
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

        TryActivateAbility(world, intent.Actor, intent.Ability);
    }
}
} // namespace

bool TryActivateAbility(World& world, EntityId actor, AbilityId ability)
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
    if (def->OnActivate.IsValid())
        ApplyEffect(world, actor, def->OnActivate);
    return true;
}

void ProcessAbilityActivations(World& world)
{
    ProcessAbilityActivationsImpl(world, nullptr);
}

void ProcessAbilityActivations(
    World& world,
    const StoragePartitionSet& partitions)
{
    ProcessAbilityActivationsImpl(world, &partitions);
}
