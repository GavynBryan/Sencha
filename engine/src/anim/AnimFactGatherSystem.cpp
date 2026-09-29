#include <anim/AnimFactGatherSystem.h>

#include <anim/AnimFactEvaluation.h>
#include <app/GameContexts.h>
#include <ecs/StoragePartitionSet.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <world/SimulationAuthority.h>

void GatherAnimFacts(const World& world,
                     EntityId entity,
                     const AnimBoundRig& rig,
                     std::span<std::uint32_t> values,
                     AnimFactHistory* history,
                     AnimTick now,
                     double tickSeconds)
{
    const AnimFactProviders* providers = world.TryGetResource<AnimFactProviders>();
    const bool hasTags = world.IsRegistered<GameplayTagContainer>();
    for (std::size_t slot = 0; slot < rig.Slots.size() && slot < values.size(); ++slot)
    {
        const AnimBoundFactSlot& bound = rig.Slots[slot];
        if (bound.Derivation >= 0)
            continue;
        if (bound.Kind == AnimFactKind::TagSet)
        {
            values[slot] = AnimFactFromBool(
                hasTags && world.TryGet<GameplayTagContainer>(entity) != nullptr);
            continue;
        }
        if (bound.Provider < 0 || providers == nullptr)
            continue;
        const AnimFactProvider& provider = providers->At(bound.Provider);
        std::uint32_t value = 0;
        if (provider.Read(world, entity, provider.Context, value))
            values[slot] = value;
    }
    if (rig.Derivations.empty())
        return;
    if (history != nullptr)
    {
        EvaluateAnimDerivations(rig, values, *history, now, tickSeconds);
        return;
    }
    // Nothing kept: the rig's derivations remember nothing from one tick to the next.
    AnimFactHistory scratch;
    EvaluateAnimDerivations(rig, values, scratch, now, tickSeconds);
}

void AnimFactGatherSystem::FixedLogic(FixedLogicContext& ctx)
{
    GatherImpl(ctx.Entities, &ctx.Partitions, AnimClockAt(ctx.Entities, ctx.Time.TickIndex), ctx.Time.DeltaSeconds);
}

void AnimFactGatherSystem::Gather(World& world, AnimTick now, double tickSeconds)
{
    GatherImpl(world, nullptr, AnimClock::Uniform(now), tickSeconds);
}

void AnimFactGatherSystem::GatherImpl(World& world, const StoragePartitionSet* partitions,
                                      const AnimClock& clock, double tickSeconds)
{
    AnimRigBindings* bindings = world.TryGetResource<AnimRigBindings>();
    if (bindings == nullptr || !world.IsRegistered<AnimRig>())
        return;

    if (LastWorld != &world)
    {
        SmallKept.reset();
        Small.reset();
        LargeKept.reset();
        Large.reset();
        LastWorld = &world;
    }

    AnimRigRunCache rigs(*bindings, world);

    const auto visit = [&]<typename Storage, bool Kept>(auto& view)
    {
        const auto handles = view.template Read<AnimRig>();
        auto facts = view.template Write<Storage>();
        for (std::uint32_t i = 0; i < view.Count(); ++i)
        {
            const AnimBoundRig* rig = rigs.Resolve(handles[i].Rig);
            std::span<std::uint32_t> values(facts[i].Values);
            // Storage the rig outgrew is recomposed before the next tick.
            if (rig == nullptr || !rig->Valid || !rig->HasFacts || !ShouldRunAnimationLogic(PresentsPose, *rig)
                || rig->Slots.size() > values.size())
                continue;
            AnimFactHistory* history = nullptr;
            if constexpr (Kept)
                history = &view.template Write<AnimFactHistory>()[i];
            GatherAnimFacts(world, view.Entity(i), *rig, values, history, clock.For(view.Entity(i)), tickSeconds);
        }
    };

    const auto run = [&](auto& query, auto&& fn)
    {
        if (partitions != nullptr)
            query.ForEachChunkIn(*partitions, fn);
        else
            query.ForEachChunk(fn);
    };
    const auto each = [&]<typename Storage, bool Kept>(auto& slot)
    {
        if (!world.IsRegistered<Storage>() || (Kept && !world.IsRegistered<AnimFactHistory>()))
            return;
        if (!slot.has_value())
            slot.emplace(world);
        run(*slot, [&](auto& view) { visit.template operator()<Storage, Kept>(view); });
    };
    each.template operator()<AnimFacts, true>(SmallKept);
    each.template operator()<AnimFacts, false>(Small);
    each.template operator()<AnimFactsLarge, true>(LargeKept);
    each.template operator()<AnimFactsLarge, false>(Large);
}
