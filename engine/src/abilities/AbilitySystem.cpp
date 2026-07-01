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

#include <ecs/World.h>

#include <utility>

namespace
{
    // Affordable if every negative additive cost modifier can be paid from the
    // attribute's Base (the pool the instant cost effect spends from).
    bool CanAfford(const World& world, EntityId actor, EffectId cost, const EffectRegistry& effects)
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

bool TryActivateAbility(World& world, EntityId actor, AbilityId ability)
{
    const AbilityRegistry* abilities = std::as_const(world).TryGetResource<AbilityRegistry>();
    if (abilities == nullptr)
        return false;
    const AbilityDefinition* def = abilities->Get(ability);
    if (def == nullptr)
        return false;

    // Actor must have been granted the ability.
    const AbilitySet* owned = std::as_const(world).TryGet<AbilitySet>(actor);
    if (owned == nullptr || !owned->Has(ability))
        return false;

    // Activation requirements: require/block tags (the cooldown tag is a block).
    if (const GameplayTagRegistry* tagReg = std::as_const(world).TryGetResource<GameplayTagRegistry>())
    {
        const GameplayTagContainer* tags = std::as_const(world).TryGet<GameplayTagContainer>(actor);
        const GameplayTagContainer empty{};
        if (!def->ActivationRequirements.Matches(tags != nullptr ? *tags : empty, *tagReg))
            return false;
    }

    // Cost affordability.
    if (const EffectRegistry* effects = std::as_const(world).TryGetResource<EffectRegistry>())
        if (!CanAfford(std::as_const(world), actor, def->Cost, *effects))
            return false;

    // Commit: pay cost, start cooldown, run behavior — all via the effect system.
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
    AbilityActivationQueue* queue = world.TryGetResource<AbilityActivationQueue>();
    if (queue == nullptr)
        return;

    // Move out before processing so intents pushed during activation are handled
    // next drain, not this one.
    std::vector<AbilityActivation> pending = std::move(queue->Pending);
    queue->Pending.clear();

    for (const AbilityActivation& intent : pending)
        TryActivateAbility(world, intent.Actor, intent.Ability);
}
