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

#include <algorithm>
#include <cmath>
#include <tuple>
#include <utility>
#include <vector>

namespace
{
// The effect passes' working state: the application counter the fold orders by,
// and buffers kept so a pass allocates nothing once warm.
struct EffectPassState
{
    struct FoldEntry
    {
        EntityId Target;
        EffectId Def;
        std::uint64_t Sequence = 0;
    };

    std::uint64_t NextSequence = 1;
    std::vector<FoldEntry> Fold;
    std::vector<EntityId> Ending;
};

EffectPassState& PassStateOf(World& world)
{
    if (EffectPassState* state = world.TryGetResource<EffectPassState>())
        return *state;
    return world.AddResource<EffectPassState>();
}

std::uint32_t TicksFor(float seconds, double tickSeconds)
{
    if (!(tickSeconds > 0.0))
        return 1;
    const long long ticks = std::llround(static_cast<double>(seconds) / tickSeconds);
    return static_cast<std::uint32_t>(std::max(1LL, ticks));
}

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

bool TargetIsActive(const World& world, EntityId target, const StoragePartitionSet* partitions)
{
    return partitions == nullptr || partitions->Contains(world.GetEntityPartition(target));
}

void EndEffect(World& world, EntityId effect)
{
    const ActiveEffect* ae = std::as_const(world).TryGet<ActiveEffect>(effect);
    if (ae == nullptr)
        return;
    if (GameplayTagContainer* tags = world.TryGet<GameplayTagContainer>(ae->Target))
        for (std::uint8_t i = 0; i < ae->GrantedTagCount; ++i)
            tags->Revoke(ae->GrantedTags[i]);
    world.DestroyEntity(effect);
}

void EndCollected(World& world, std::vector<EntityId>& ending)
{
    for (EntityId effect : ending)
        EndEffect(world, effect);
    ending.clear();
}

void TickEffectsImpl(
    World& world,
    const StoragePartitionSet* partitions)
{
    if (!world.IsRegistered<ActiveEffect>())
        return;
    const EffectRegistry* effReg = std::as_const(world).TryGetResource<EffectRegistry>();
    const AttributeRegistry* attrReg = std::as_const(world).TryGetResource<AttributeRegistry>();
    EffectPassState& state = PassStateOf(world);

    Query<Write<ActiveEffect>> query(world);
    query.ForEachChunk([&](auto& view)
    {
        auto effects = view.template Write<ActiveEffect>();
        const EntityIndex* indices = view.Entities();
        for (std::uint32_t i = 0; i < view.Count(); ++i)
        {
            ActiveEffect& ae = effects[i];
            const EntityId self = world.ResolveEntityIndex(indices[i]);
            if (!world.IsAlive(ae.Target) || (ae.Owner.IsSet() && !world.IsAlive(ae.Owner.Entity)))
            {
                state.Ending.push_back(self);
                continue;
            }
            if (!TargetIsActive(world, ae.Target, partitions))
                continue;

            const EffectDefinition* def = effReg != nullptr ? effReg->Get(ae.Def) : nullptr;
            if (def == nullptr)
            {
                state.Ending.push_back(self);
                continue;
            }
            if (ae.PeriodTicks > 0 && --ae.TicksToPeriod == 0)
            {
                if (AttributeSet* set = world.TryGet<AttributeSet>(ae.Target))
                    ApplyModifiersToBase(*set, *def, attrReg);
                ae.TicksToPeriod = ae.PeriodTicks;
            }
            if (ae.RemainingTicks != kEffectNeverEnds && --ae.RemainingTicks == 0)
                state.Ending.push_back(self);
        }
    });

    EndCollected(world, state.Ending);
}

void FoldActiveEffectsImpl(
    World& world,
    const StoragePartitionSet* partitions)
{
    const EffectRegistry* effReg = std::as_const(world).TryGetResource<EffectRegistry>();
    if (effReg == nullptr || !world.IsRegistered<ActiveEffect>())
        return;
    EffectPassState& state = PassStateOf(world);

    state.Fold.clear();
    Query<Read<ActiveEffect>> query(world);
    query.ForEachChunk([&](auto& view)
    {
        const auto effects = view.template Read<ActiveEffect>();
        for (std::uint32_t i = 0; i < view.Count(); ++i)
        {
            const ActiveEffect& ae = effects[i];
            if (ae.PeriodTicks > 0 || !world.IsAlive(ae.Target) || !TargetIsActive(world, ae.Target, partitions))
                continue;
            state.Fold.push_back({ ae.Target, ae.Def, ae.Sequence });
        }
    });

    // Floating-point folds depend on order, and storage order depends on this
    // World's history, so the fold walks a canonical order instead.
    std::sort(state.Fold.begin(), state.Fold.end(), [](const auto& a, const auto& b) {
        return std::tie(a.Target.Index, a.Target.Generation, a.Def.Value, a.Sequence)
             < std::tie(b.Target.Index, b.Target.Generation, b.Def.Value, b.Sequence);
    });

    for (ModifierOp op : { ModifierOp::Add, ModifierOp::Multiply, ModifierOp::Override })
    {
        for (const EffectPassState::FoldEntry& entry : state.Fold)
        {
            const EffectDefinition* def = effReg->Get(entry.Def);
            AttributeSet* set = world.TryGet<AttributeSet>(entry.Target);
            if (def == nullptr || set == nullptr)
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
    }
}
} // namespace

EntityId ApplyEffect(World& world, EntityId target, EffectId effect, const FixedSimTime& now,
                     EffectOwner owner)
{
    const EffectRegistry* effReg =
        std::as_const(world).TryGetResource<EffectRegistry>();
    if (effReg == nullptr || !world.IsAlive(target))
        return {};
    const EffectDefinition* def = effReg->Get(effect);
    if (def == nullptr)
        return {};

    if (def->Duration == EffectDuration::Instant)
    {
        const AttributeRegistry* attrReg =
            std::as_const(world).TryGetResource<AttributeRegistry>();
        if (AttributeSet* set = world.TryGet<AttributeSet>(target))
            ApplyModifiersToBase(*set, *def, attrReg);
        return {};
    }

    ActiveEffect ae{};
    ae.Target = target;
    ae.Def = effect;
    ae.Owner = owner;
    ae.Sequence = PassStateOf(world).NextSequence++;
    if (def->Duration == EffectDuration::Duration)
        ae.RemainingTicks = TicksFor(def->DurationSeconds, now.DeltaSeconds);
    if (def->Period > 0.0f)
    {
        ae.PeriodTicks = TicksFor(def->Period, now.DeltaSeconds);
        ae.TicksToPeriod = ae.PeriodTicks;
    }

    // Only what was granted is recorded, so ending never revokes a stack it did not add.
    if (GameplayTagContainer* tags = world.TryGet<GameplayTagContainer>(target))
    {
        for (GameplayTagId tag : def->GrantedTags)
        {
            const bool fits = tag.IsValid()
                && (tags->StackCount(tag) > 0 || tags->Size() < GameplayTagContainer::Capacity);
            if (!fits || ae.GrantedTagCount == kEffectGrantedTagCapacity)
                continue;
            tags->Grant(tag);
            ae.GrantedTags[ae.GrantedTagCount++] = tag;
        }
    }

    // Persistent, so zone moves and detaches never strand the tags it granted.
    const EntityId fx = world.CreateEntity(StoragePartitionId::Default());
    world.AddComponent<ActiveEffect>(fx, ae);
    return fx;
}

bool RemoveEffect(World& world, EntityId effect)
{
    if (!world.IsAlive(effect) || !world.HasComponent<ActiveEffect>(effect))
        return false;
    EndEffect(world, effect);
    return true;
}

std::size_t RemoveEffectsOwnedBy(World& world, const EffectOwner& owner)
{
    if (!owner.IsSet() || !world.IsRegistered<ActiveEffect>())
        return 0;
    EffectPassState& state = PassStateOf(world);

    Query<Read<ActiveEffect>> query(world);
    query.ForEachChunk([&](auto& view)
    {
        const auto effects = view.template Read<ActiveEffect>();
        const EntityIndex* indices = view.Entities();
        for (std::uint32_t i = 0; i < view.Count(); ++i)
            if (effects[i].Owner == owner)
                state.Ending.push_back(world.ResolveEntityIndex(indices[i]));
    });

    const std::size_t ended = state.Ending.size();
    EndCollected(world, state.Ending);
    return ended;
}

void TickEffects(World& world)
{
    TickEffectsImpl(world, nullptr);
}

void TickEffects(
    World& world,
    const StoragePartitionSet& partitions)
{
    TickEffectsImpl(world, &partitions);
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
