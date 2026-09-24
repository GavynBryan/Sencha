#pragma once

#include <anim/AnimRequests.h>

#include <cstddef>
#include <cstdint>
#include <vector>

class World;

//=============================================================================
// AnimRequestJournal
//
// The animation requests a client issued ahead of the authority's word, kept
// until the authority has had it. Gameplay issues through this rather than
// straight into a request set, and gets the same answer on every machine: on
// the authority, and with no session, the request is simply issued; on a
// client it is issued as a prediction and remembered with the command whose
// processing issues it for real.
//
// A snapshot replaces a request set with the authority's, which wipes every
// prediction on it. After each one, Reconcile forgets what the authority has
// decided -- every prediction whose command it has processed, whether its set
// now holds the request or it refused -- and takes that prediction off the set
// if a snapshot did not already, then issues what is still undecided again on
// top. Nothing in animation is rewound: content follows the set it is given,
// and a corrected start restarts that content where the authority put it.
//
// Narrow on purpose: issuing is all a client predicts. Cancelling, anchors
// and tails are the authority's.
//=============================================================================
class AnimRequestJournal
{
public:
    // Bounds what one client may have in flight; a prediction past it is
    // issued unrecorded and left to the authority's next word.
    static constexpr std::size_t kCapacity = 32;

    AnimRequestResult Issue(World& world, EntityId animated, const AnimRequestDesc& desc, AnimTick now,
                            std::uint64_t commandTick);

    // After a snapshot has been applied, with the last command the authority
    // said it processed.
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
