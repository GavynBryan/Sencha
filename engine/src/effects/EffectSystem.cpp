#include <effects/EffectSystem.h>

#include <attributes/AttributeRegistry.h>
#include <attributes/AttributeResolve.h>
#include <attributes/AttributeSet.h>
#include <effects/ActiveEffect.h>
#include <effects/EffectDefinition.h>
#include <effects/EffectRegistry.h>
#include <gameplay_tags/GameplayTagContainer.h>

#include <ecs/Query.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>

#include <utility>
#include <vector>

namespace
{
void ApplyModifiersToBase(
    AttributeSet& set,
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

template <typename QueryType, typename Visit>
void VisitEffectChunks(
    QueryType& query,
    const StoragePartitionSet* partitions,
    Visit&& visit)
{
    if (partitions != nullptr)
        query.ForEachChunkIn(*partitions, std::forward<Visit>(visit));
    else
        query.ForEachChunk(std::forward<Visit>(visit));
}

void TickEffectsImpl(
    World& world,
    float dt,
    const StoragePartitionSet* partitions)
{
    const EffectRegistry* effReg =
        std::as_const(world).TryGetResource<EffectRegistry>();
    if (effReg == nullptr || !world.IsRegistered<ActiveEffect>())
        return;
    const AttributeRegistry* attrReg =
        std::as_const(world).TryGetResource<AttributeRegistry>();

    std::vector<EntityId> expired;
    Query<Write<ActiveEffect>> query(world);
    VisitEffectChunks(query, partitions, [&](auto& view)
    {
        auto effects = view.template Write<ActiveEffect>();
        const EntityIndex* indices = view.Entities();
        for (std::uint32_t i = 0; i < view.Count(); ++i)
        {
            ActiveEffect& ae = effects[i];
            const EffectDefinition* def = effReg->Get(ae.Def);
            const EntityId effectEntity = world.ResolveEntity(indices[i]);
            if (def == nullptr)
            {
                expired.push_back(effectEntity);
                continue;
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

            if (ae.TimeRemaining >= 0.0f)
            {
                ae.TimeRemaining -= dt;
                if (ae.TimeRemaining <= 0.0f)
                    expired.push_back(effectEntity);
            }
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

void FoldActiveEffectsImpl(
    World& world,
    const StoragePartitionSet* partitions)
{
    const EffectRegistry* effReg =
        std::as_const(world).TryGetResource<EffectRegistry>();
    if (effReg == nullptr || !world.IsRegistered<ActiveEffect>())
        return;

    auto foldPass = [&](ModifierOp op)
    {
        Query<Read<ActiveEffect>> query(world);
        VisitEffectChunks(query, partitions, [&](auto& view)
        {
            const auto effects = view.template Read<ActiveEffect>();
            for (std::uint32_t i = 0; i < view.Count(); ++i)
            {
                const ActiveEffect& ae = effects[i];
                const EffectDefinition* def = effReg->Get(ae.Def);
                if (def == nullptr || def->Period > 0.0f)
                    continue;

                AttributeSet* set = world.TryGet<AttributeSet>(ae.Target);
                if (set == nullptr)
                    continue;

                for (const EffectModifier& m : def->Modifiers)
                {
                    if (m.Op != op)
                        continue;
                    float* c = set->CurrentPtr(m.Attr);
                    if (c == nullptr)
                        continue;
                    switch (op)
                    {
                    case ModifierOp::Add: *c += m.Magnitude; break;
                    case ModifierOp::Multiply: *c *= m.Magnitude; break;
                    case ModifierOp::Override: *c = m.Magnitude; break;
                    }
                }
            }
        });
    };

    foldPass(ModifierOp::Add);
    foldPass(ModifierOp::Multiply);
    foldPass(ModifierOp::Override);
}
} // namespace

void ApplyEffect(World& world, EntityId target, EffectId effect)
{
    const EffectRegistry* effReg =
        std::as_const(world).TryGetResource<EffectRegistry>();
    if (effReg == nullptr || !world.IsAlive(target))
        return;
    const EffectDefinition* def = effReg->Get(effect);
    if (def == nullptr)
        return;

    if (def->Duration == EffectDuration::Instant)
    {
        const AttributeRegistry* attrReg =
            std::as_const(world).TryGetResource<AttributeRegistry>();
        if (AttributeSet* set = world.TryGet<AttributeSet>(target))
            ApplyModifiersToBase(*set, *def, attrReg);
        return;
    }

    const StoragePartitionId partition = world.GetEntityPartition(target);
    EntityId fx = world.CreateEntity(partition);
    ActiveEffect ae{};
    ae.Target = target;
    ae.Def = effect;
    ae.TimeRemaining =
        def->Duration == EffectDuration::Infinite ? -1.0f : def->DurationSeconds;
    ae.PeriodTimer = def->Period;
    world.AddComponent<ActiveEffect>(fx, ae);

    if (GameplayTagContainer* tags = world.TryGet<GameplayTagContainer>(target))
        for (GameplayTagId tag : def->GrantedTags)
            tags->Grant(tag);
}

void TickEffects(World& world, float dt)
{
    TickEffectsImpl(world, dt, nullptr);
}

void TickEffects(
    World& world,
    float dt,
    const StoragePartitionSet& partitions)
{
    TickEffectsImpl(world, dt, &partitions);
}

void FoldActiveEffects(World& world)
{
    FoldActiveEffectsImpl(world, nullptr);
}

void FoldActiveEffects(
    World& world,
    const StoragePartitionSet& partitions)
{
    FoldActiveEffectsImpl(world, &partitions);
}

void ResolveAttributesWithEffects(World& world)
{
    ResetAttributesToBase(world);
    FoldActiveEffects(world);
    ClampAttributes(world);
}

void ResolveAttributesWithEffects(
    World& world,
    const StoragePartitionSet& partitions)
{
    ResetAttributesToBase(world, partitions);
    FoldActiveEffects(world, partitions);
    ClampAttributes(world, partitions);
}
