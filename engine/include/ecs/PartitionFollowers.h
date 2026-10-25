#pragma once

#include <ecs/World.h>

#include <cstdint>
#include <span>
#include <vector>

// Entities that must share a storage partition with another one -- what is
// attached to a body, what a container holds -- follow it through registered
// operations. World records every partition move; DispatchPartitionMoves hands
// each batch to every follower, which may move more entities, and repeats until
// a batch adds nothing. See docs/architecture/runtime-world.md.
using PartitionFollowFn = void (*)(void* context, World& world, std::span<const EntityPartitionMove> moves);

struct PartitionFollowerId
{
    std::uint32_t Value = 0;

    [[nodiscard]] bool IsValid() const { return Value != 0; }
    friend bool operator==(PartitionFollowerId, PartitionFollowerId) = default;
};

// A World resource. A follower runs on the owner thread, outside any query.
class PartitionFollowers
{
public:
    [[nodiscard]] PartitionFollowerId Register(void* context, PartitionFollowFn follow);
    void Unregister(PartitionFollowerId id);

    void Follow(World& world, std::span<const EntityPartitionMove> moves) const;
    [[nodiscard]] std::size_t Count() const { return Entries.size(); }

private:
    struct Entry
    {
        PartitionFollowerId Id;
        void* Context = nullptr;
        PartitionFollowFn Fn = nullptr;
    };
    std::vector<Entry> Entries;
    std::uint32_t NextId = 1;
};

// Consumes the World's partition moves, showing each batch to the followers
// until none is left. Returns how many moves it consumed.
std::size_t DispatchPartitionMoves(World& world);
