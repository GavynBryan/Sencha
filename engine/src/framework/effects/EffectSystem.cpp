#include <framework/effects/EffectSystem.h>

#include <framework/attributes/AttributeRegistry.h>
#include <framework/attributes/AttributeResolve.h>
#include <framework/attributes/AttributeSet.h>
#include <framework/effects/ActiveEffect.h>
#include <framework/effects/EffectDefinition.h>
#include <framework/effects/EffectRegistry.h>
#include <framework/gameplay_tags/GameplayTagContainer.h>

#include <ecs/World.h>

#include <utility>
#include <vector>

namespace
{
    // Apply a definition's modifiers to Base (Add -> Multiply -> Override), then
    // clamp every base value to its attribute range. Used by instant and
    // periodic application.
    void ApplyModifiersToBase(AttributeSet& set,
                              const EffectDefinition& def,
                              const AttributeRegistry* attrReg)
    {
        for (const EffectModifier& m : def.Modifiers)
            if (m.Op == ModifierOp::Add)
                if (float* b = set.BasePtr(m.Attr)) *b += m.Magnitude;
        for (const EffectModifier& m : def.Modifiers)
            if (m.Op == ModifierOp::Multiply)
                if (float* b = set.BasePtr(m.Attr)) *b *= m.Magnitude;
        for (const EffectModifier& m : def.Modifiers)
            if (m.Op == ModifierOp::Override)
                if (float* b = set.BasePtr(m.Attr)) *b = m.Magnitude;

        if (attrReg != nullptr)
            for (int i = 0; i < set.Count; ++i)
                set.Base[i] = attrReg->Clamp(set.Ids[i], set.Base[i]);
    }
}

void ApplyEffect(World& world, EntityId target, EffectId effect)
{
    const EffectRegistry* effReg = std::as_const(world).TryGetResource<EffectRegistry>();
    if (effReg == nullptr)
        return;
    const EffectDefinition* def = effReg->Get(effect);
    if (def == nullptr)
        return;

    if (def->Duration == EffectDuration::Instant)
    {
        const AttributeRegistry* attrReg = std::as_const(world).TryGetResource<AttributeRegistry>();
        if (AttributeSet* set = world.TryGet<AttributeSet>(target))
            ApplyModifiersToBase(*set, *def, attrReg);
        return;
    }

    // Duration / Infinite: an ActiveEffect entity carries the application; tags
    // are granted to the target for its lifetime.
    EntityId fx = world.CreateEntity();
    ActiveEffect ae{};
    ae.Target = target;
    ae.Def = effect;
    ae.TimeRemaining = (def->Duration == EffectDuration::Infinite) ? -1.0f : def->DurationSeconds;
    ae.PeriodTimer = def->Period; // first periodic application after one full period
    world.AddComponent<ActiveEffect>(fx, ae);

    if (GameplayTagContainer* tags = world.TryGet<GameplayTagContainer>(target))
        for (GameplayTagId tag : def->GrantedTags)
            tags->Grant(tag);
}

void TickEffects(World& world, float dt)
{
    const EffectRegistry* effReg = std::as_const(world).TryGetResource<EffectRegistry>();
    if (effReg == nullptr)
        return;
    const AttributeRegistry* attrReg = std::as_const(world).TryGetResource<AttributeRegistry>();

    std::vector<EntityId> expired;
    world.ForEachComponent<ActiveEffect>([&](EntityId entity, ActiveEffect& ae)
    {
        const EffectDefinition* def = effReg->Get(ae.Def);
        if (def == nullptr)
        {
            expired.push_back(entity); // definition gone: drop the orphan
            return;
        }

        if (def->Period > 0.0f)
        {
            ae.PeriodTimer -= dt;
            int guard = 0;
            while (ae.PeriodTimer <= 0.0f && guard++ < 64)
            {
                if (AttributeSet* set = world.TryGet<AttributeSet>(ae.Target))
                    ApplyModifiersToBase(*set, *def, attrReg);
                ae.PeriodTimer += def->Period;
            }
        }

        if (ae.TimeRemaining >= 0.0f) // finite (negative == infinite)
        {
            ae.TimeRemaining -= dt;
            if (ae.TimeRemaining <= 0.0f)
                expired.push_back(entity);
        }
    });

    for (EntityId entity : expired)
    {
        if (const ActiveEffect* ae = world.TryGet<ActiveEffect>(entity))
            if (const EffectDefinition* def = effReg->Get(ae->Def))
                if (GameplayTagContainer* tags = world.TryGet<GameplayTagContainer>(ae->Target))
                    for (GameplayTagId tag : def->GrantedTags)
                        tags->Revoke(tag);

        world.DestroyEntity(entity);
    }
}

void FoldActiveEffects(World& world)
{
    const EffectRegistry* effReg = std::as_const(world).TryGetResource<EffectRegistry>();
    if (effReg == nullptr)
        return;

    auto foldPass = [&world, effReg](ModifierOp op)
    {
        std::as_const(world).ForEachComponent<ActiveEffect>(
            [&world, effReg, op](EntityId, const ActiveEffect& ae)
        {
            const EffectDefinition* def = effReg->Get(ae.Def);
            if (def == nullptr || def->Period > 0.0f)
                return; // periodic effects modify Base, not Current
            AttributeSet* set = world.TryGet<AttributeSet>(ae.Target);
            if (set == nullptr)
                return;
            for (const EffectModifier& m : def->Modifiers)
            {
                if (m.Op != op)
                    continue;
                float* c = set->CurrentPtr(m.Attr);
                if (c == nullptr)
                    continue;
                switch (op)
                {
                    case ModifierOp::Add:      *c += m.Magnitude; break;
                    case ModifierOp::Multiply: *c *= m.Magnitude; break;
                    case ModifierOp::Override: *c  = m.Magnitude; break;
                }
            }
        });
    };

    foldPass(ModifierOp::Add);
    foldPass(ModifierOp::Multiply);
    foldPass(ModifierOp::Override);
}

void ResolveAttributesWithEffects(World& world)
{
    ResetAttributesToBase(world);
    FoldActiveEffects(world);
    ClampAttributes(world);
}
