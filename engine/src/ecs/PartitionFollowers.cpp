#include <ecs/PartitionFollowers.h>

#include <algorithm>
#include <cassert>

PartitionFollowerId PartitionFollowers::Register(void* context, PartitionFollowFn follow)
{
    assert(follow != nullptr);
    const PartitionFollowerId id{ NextId++ };
    Entries.push_back(Entry{ id, context, follow });
    return id;
}

void PartitionFollowers::Unregister(PartitionFollowerId id)
{
    std::erase_if(Entries, [&](const Entry& entry) { return entry.Id == id; });
}

void PartitionFollowers::Follow(World& world, std::span<const EntityPartitionMove> moves) const
{
    for (const Entry& entry : Entries)
        entry.Fn(entry.Context, world, moves);
}

std::size_t DispatchPartitionMoves(World& world)
{
    // A follower moving an entity onto a partition it already left would make
    // the rounds endless; each round is one level of following, and no real
    // chain of dependents is this deep.
    constexpr int kMaxRounds = 64;
    std::size_t consumed = 0;
    for (int round = 0; round < kMaxRounds; ++round)
    {
        const std::vector<EntityPartitionMove> moves = world.ConsumePartitionMoves();
        if (moves.empty())
            return consumed;
        consumed += moves.size();
        if (const PartitionFollowers* followers = world.TryGetResource<PartitionFollowers>())
            followers->Follow(world, moves);
    }
    assert(false && "partition followers kept moving entities after 64 rounds");
    return consumed;
}
