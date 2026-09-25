#pragma once

#include <anim/AnimRequests.h>

#include <cstddef>
#include <cstdint>
#include <vector>

class World;

// Requests a client issued ahead of the authority, kept until the authority has
// decided them. Only issuing is predicted; cancels, anchors and tails are the authority's.
class AnimRequestJournal
{
public:
    // Bounds what one client may have in flight; a prediction past it is
    // issued unrecorded and left to the authority's next word.
    static constexpr std::size_t kCapacity = 32;

    AnimRequestResult Issue(World& world, EntityId animated, const AnimRequestDesc& desc, AnimTick now,
                            std::uint64_t commandTick);

    // Call after applying a snapshot, with the last command the authority processed.
    void Reconcile(World& world, std::uint64_t acknowledgedCommand);

    [[nodiscard]] std::size_t Size() const { return Entries.size(); }
    void Clear() { Entries.clear(); }

private:
    struct Entry
    {
        EntityId Animated;
        AnimRequestDesc Desc;
        AnimTick IssuedTick = 0;
        std::uint64_t CommandTick = 0;
    };

    std::vector<Entry> Entries;
};
