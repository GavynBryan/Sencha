#include <anim/AnimFactGatherSystem.h>

#include <anim/AnimFactEvaluation.h>
#include <app/GameContexts.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/StoragePartitionSet.h>
#include <gameplay_tags/GameplayTagContainer.h>

void GatherAnimFacts(const World& world,
                     EntityId entity,
                     const AnimBoundRig& rig,
                     std::span<std::uint32_t> values,
                     AnimFactHistory& history,
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
    EvaluateAnimDerivations(rig, values, history, now, tickSeconds);
}

void AnimFactGatherSystem::FixedLogic(FixedLogicContext& ctx)
{
    GatherImpl(ctx.Entities, &ctx.Partitions, ctx.Time.TickIndex, ctx.Time.DeltaSeconds);
}

void AnimFactGatherSystem::Gather(World& world, AnimTick now, double tickSeconds)
{
    GatherImpl(world, nullptr, now, tickSeconds);
}

const AnimBoundRig* AnimFactGatherSystem::Bind(AnimRigBindings& bindings, DataAssetHandle rig,
                                               const World& world)
{
    const AnimBoundRig* bound = bindings.Resolve(rig, world);
    if (bound == nullptr)
        return nullptr;
    if (!bound->Diagnostics.empty() && Logging != nullptr
        && Reported.insert(bound->Generation).second)
    {
        auto& logger = Logging->GetLogger<AnimFactGatherSystem>();
        for (const AnimDiagnostic& diagnostic : bound->Diagnostics)
        {
            if (diagnostic.Severity == AnimDiagnosticSeverity::Error)
                logger.Error(FormatAnimDiagnostic(diagnostic));
            else
                logger.Warn(FormatAnimDiagnostic(diagnostic));
        }
    }
    return bound->Valid && bound->HasFacts ? bound : nullptr;
}

void AnimFactGatherSystem::GatherImpl(World& world, const StoragePartitionSet* partitions,
                                      AnimTick now, double tickSeconds)
{
    AnimRigBindings* bindings = world.TryGetResource<AnimRigBindings>();
    if (bindings == nullptr || !world.IsRegistered<AnimRig>()
        || !world.IsRegistered<AnimFactHistory>())
    {
        return;
    }

    if (LastWorld != &world)
    {
        SmallQuery.reset();
        LargeQuery.reset();
        LastWorld = &world;
    }

    // Entities sharing a rig tend to share chunks, so the binding is resolved
    // once per run of equal handles rather than once per entity.
    DataAssetHandle lastHandle{};
    const AnimBoundRig* lastRig = nullptr;
    bool haveLast = false;

    // A rig laid out wider than the entity's storage cannot be gathered into
    // it; the entity keeps its last snapshot and the mismatch is logged once.
    const auto visit = [&]<typename Storage>(auto& view)
    {
        const auto rigs = view.template Read<AnimRig>();
        auto facts = view.template Write<Storage>();
        auto histories = view.template Write<AnimFactHistory>();
        for (std::uint32_t i = 0; i < view.Count(); ++i)
        {
            if (!haveLast || rigs[i].Rig != lastHandle)
            {
                lastHandle = rigs[i].Rig;
                lastRig = Bind(*bindings, lastHandle, world);
                haveLast = true;
            }
            const AnimBoundRig* rig = lastRig;
            if (rig == nullptr)
                continue;
            std::span<std::uint32_t> values(facts[i].Values);
            if (rig->Slots.size() > values.size())
            {
                if (Logging != nullptr && ReportedCapacity.insert(rig->Generation).second)
                    Logging->GetLogger<AnimFactGatherSystem>().Error(
                        "'{}' lays out {} facts and an entity using it carries storage for {}; "
                        "give the entity the storage its rig's fact_capacity names.",
                        rig->RigPath, rig->Slots.size(), values.size());
                continue;
            }
            GatherAnimFacts(world, view.Entity(i), *rig, values, histories[i], now, tickSeconds);
        }
    };

    const auto run = [&](auto& query, auto&& fn)
    {
        if (partitions != nullptr)
            query.ForEachChunkIn(*partitions, fn);
        else
            query.ForEachChunk(fn);
    };

    if (world.IsRegistered<AnimFacts>())
    {
        if (!SmallQuery.has_value())
            SmallQuery.emplace(world);
        run(*SmallQuery, [&](auto& view) { visit.template operator()<AnimFacts>(view); });
    }
    if (world.IsRegistered<AnimFactsLarge>())
    {
        if (!LargeQuery.has_value())
            LargeQuery.emplace(world);
        run(*LargeQuery, [&](auto& view) { visit.template operator()<AnimFactsLarge>(view); });
    }
}
