#include <time/ActorTimeSystem.h>

#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>

namespace
{
    void Advance(Query<Write<ActorTime>>& clocks, const StoragePartitionSet* partitions)
    {
        clocks.ForEachChunk([&](auto& view) {
            if (partitions != nullptr && !partitions->Contains(view.Partition()))
                return;
            auto times = view.template Write<ActorTime>();
            for (std::uint32_t i = 0; i < view.Count(); ++i)
                times[i].LocalQ16 += times[i].ScaleQ16;
        });
    }
}

void ActorTimeSystem::FixedLogic(FixedLogicContext& ctx)
{
    if (!ctx.Entities.IsRegistered<ActorTime>())
        return;
    if (!Clocks)
        Clocks.emplace(ctx.Entities);
    Advance(*Clocks, &ctx.Partitions);
}

void AdvanceActorTime(World& world)
{
    if (!world.IsRegistered<ActorTime>())
        return;
    Query<Write<ActorTime>> clocks(world);
    Advance(clocks, nullptr);
}
