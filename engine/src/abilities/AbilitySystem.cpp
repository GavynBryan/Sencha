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

#include <ecs/EntityStore.h>

#include <utility>

namespace
{
    // Affordable if every negative additive cost modifier can be paid from the
    // attribute's Base (the pool the instant cost effect spends from).
    bool CanAfford(const EntityStore& world, EntityId actor, EffectId cost, const EffectRegistry& effects)
    {
        if (!cost.IsValid())
            return true;
        const EffectDefinition* def = effects.Get(cost);
        if (def == nullptr)
            return true;
        const AttributeSet* set = world.TryGet<AttributeSet>(actor);
        if (set == nullptr)
            return true; // nothing to charge against

        for (const EffectModifier& m : def->Modifiers)
            if (m.Op == ModifierOp::Add && m.Magnitude < 0.0f)
                if (set->GetBase(m.Attr, 0.0f) < -m.Magnitude)
                    return false;
        return true;
    }
}

bool TryActivateAbility(EntityStore& world, EntityId actor, AbilityId ability,
                        const AbilityRegistry& abilities,
                        const GameplayTagRegistry& tags,
                        const EffectRegistry& effects,
                        const AttributeRegistry& attributes)
{
    const AbilityDefinition* def = abilities.Get(ability);
    if (def == nullptr)
        return false;

    // Actor must have been granted the ability.
    const AbilitySet* owned = std::as_const(world).TryGet<AbilitySet>(actor);
    if (owned == nullptr || !owned->Has(ability))
        return false;

    // Activation requirements: require/block tags (the cooldown tag is a block).
    {
        const GameplayTagContainer* actorTags = std::as_const(world).TryGet<GameplayTagContainer>(actor);
        const GameplayTagContainer empty{};
        if (!def->ActivationRequirements.Matches(actorTags != nullptr ? *actorTags : empty, tags))
            return false;
    }

    // Cost affordability.
    if (!CanAfford(std::as_const(world), actor, def->Cost, effects))
        return false;

    // Commit: pay cost, start cooldown, run behavior — all via the effect system.
    if (def->Cost.IsValid())
        ApplyEffect(world, actor, def->Cost, effects, attributes);
    if (def->Cooldown.IsValid())
        ApplyEffect(world, actor, def->Cooldown, effects, attributes);
    if (def->OnActivate.IsValid())
        ApplyEffect(world, actor, def->OnActivate, effects, attributes);
    return true;
}

void ProcessAbilityActivations(EntityStore& world,
                               AbilityActivationQueue& queue,
                               const AbilityRegistry& abilities,
                               const GameplayTagRegistry& tags,
                               const EffectRegistry& effects,
                               const AttributeRegistry& attributes)
{
    // Move out before processing so intents pushed during activation are handled
    // next drain, not this one.
    std::vector<AbilityActivation> pending = std::move(queue.Pending);
    queue.Pending.clear();

    for (const AbilityActivation& intent : pending)
        TryActivateAbility(world, intent.Actor, intent.Ability, abilities, tags, effects, attributes);
}
