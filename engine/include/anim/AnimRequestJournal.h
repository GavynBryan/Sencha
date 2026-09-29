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
    // Bounds what one client may have in flight. Past it nothing more is predicted,
    // so every guess on a set is one this journal will take down.
    static constexpr std::size_t kCapacity = 32;

    // Issues `desc` as a prediction at `now`, the predicted entity's command-timeline tick.
    AnimRequestResult Predict(World& world, EntityId animated, AnimRequestDesc desc, AnimTick now);

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
    };

    std::vector<Entry> Entries;
};

// How gameplay asks animation for something. The authority issues it on the tick
// `animated` is simulating; a client predicts it for the entity it predicts and
// leaves every other entity's requests to the authority's word.
AnimRequestResult RequestAnimation(World& world, EntityId animated, AnimRequestDesc desc, std::uint64_t localTick);

// Ends a request its producer holds. Cancels are the authority's: a client leaves
// them to the authority's next word and answers false.
bool CancelAnimation(World& world, EntityId animated, AnimRequestId id, AnimCancelReason reason,
                     std::uint64_t localTick);
