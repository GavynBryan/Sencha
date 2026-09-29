#include <movement/RootMotionSource.h>

#include <app/GameContexts.h>
#include <ecs/Query.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <movement/components/CharacterMovement.h>
#include <movement/components/MotionChannels.h>
#include <world/SimulationTimeline.h>

#include <cmath>

bool SampleRootMotion(World& world, EntityId entity, std::uint64_t tick, double tickSeconds, RootMotionSample& out)
{
    const RootMotionSource* source = world.TryGetResource<RootMotionSource>();
    if (source == nullptr || source->Sample == nullptr)
        return false;
    out = RootMotionSample{};
    return source->Sample(world, entity, tick, tickSeconds, out);
}

void ApplyRootMotion(MotionAxisOverride& overrides, const RootMotionSample& sample)
{
    overrides.PlanarVelocity = sample.PlanarVelocity;
    overrides.HasPlanar = true;
    overrides.ForcedPlanar = true;
    if (std::isfinite(sample.TurnRadians) && sample.TurnRadians != 0.0f)
    {
        overrides.TurnRadians = sample.TurnRadians;
        overrides.HasTurn = true;
    }
}

void RootMotionSystem::FixedLogic(FixedLogicContext& ctx)
{
    StepImpl(ctx.Entities, &ctx.Partitions, ctx.Time.TickIndex, true, ctx.Time.DeltaSeconds);
}

void RootMotionSystem::Step(World& world, std::uint64_t tick, double tickSeconds)
{
    StepImpl(world, nullptr, tick, false, tickSeconds);
}

void RootMotionSystem::StepImpl(World& world, const StoragePartitionSet* partitions, std::uint64_t localTick,
                                bool timeline, double tickSeconds)
{
    const RootMotionSource* source = world.TryGetResource<RootMotionSource>();
    if (source == nullptr || source->Sample == nullptr)
        return;
    // Entities first, then samples: the source reads the World, and a query
    // holding chunk pointers while it does is one structural change from
    // reading freed memory.
    Characters.clear();
    if (LastWorld != &world)
    {
        CharacterQuery.reset();
        LastWorld = &world;
    }
    if (!CharacterQuery)
        CharacterQuery.emplace(world);
    auto& query = *CharacterQuery;
    const auto visit = [&](auto& view) {
        for (std::uint32_t row = 0; row < view.Count(); ++row)
            Characters.push_back(view.Entity(row));
    };
    if (partitions != nullptr)
        query.ForEachChunkIn(*partitions, visit);
    else
        query.ForEachChunk(visit);
    for (const EntityId character : Characters)
    {
        // The character this process predicts moves on its command timeline, as its
        // replay and the authority's run of the same command do.
        const std::uint64_t tick = timeline ? SimulationTickOf(world, character, localTick) : localTick;
        RootMotionSample sample;
        if (!source->Sample(world, character, tick, tickSeconds, sample))
            continue;
        if (MotionAxisOverride* overrides = world.TryGet<MotionAxisOverride>(character))
            ApplyRootMotion(*overrides, sample);
    }
}
