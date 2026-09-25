#include <movement/RootMotionSource.h>

#include <app/GameContexts.h>
#include <ecs/Query.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <movement/components/CharacterMovement.h>
#include <movement/components/MotionChannels.h>
#include <world/SimulationAuthority.h>

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
    StepImpl(ctx.Entities, &ctx.Partitions, AuthorityTickOf(ctx.Entities, ctx.Time.TickIndex), ctx.Time.DeltaSeconds);
}

void RootMotionSystem::Step(World& world, std::uint64_t tick, double tickSeconds)
{
    StepImpl(world, nullptr, tick, tickSeconds);
}

void RootMotionSystem::StepImpl(World& world, const StoragePartitionSet* partitions, std::uint64_t tick,
                                double tickSeconds)
{
    const RootMotionSource* source = world.TryGetResource<RootMotionSource>();
    if (source == nullptr || source->Sample == nullptr)
        return;
    // Entities first, then samples: the source reads the World, and a query
    // holding chunk pointers while it does is one structural change from
    // reading freed memory.
    Characters.clear();
    Query<Read<CharacterMovement>, Read<MotionAxisOverride>> query(world);
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
        RootMotionSample sample;
        if (!source->Sample(world, character, tick, tickSeconds, sample))
            continue;
        if (MotionAxisOverride* overrides = world.TryGet<MotionAxisOverride>(character))
            ApplyRootMotion(*overrides, sample);
    }
}
