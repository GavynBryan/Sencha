#include <anim/AnimRequestJournal.h>

#include <anim/AnimContentState.h>
#include <ecs/World.h>
#include <world/SimulationAuthority.h>
#include <world/SimulationTimeline.h>

#include <algorithm>

namespace
{
    AnimRequestSet* SetOf(World& world, EntityId animated)
    {
        return world.IsAlive(animated) && world.IsRegistered<AnimRequestSet>() ? world.TryGet<AnimRequestSet>(animated)
                                                                                : nullptr;
    }

    // A prediction and the authority's record of the same thing are one command's
    // request: the same source asking for the same intent.
    bool SameRequest(const AnimRequest& request, const AnimRequestDesc& desc)
    {
        return request.Occupied && request.Id.Source == desc.Source && request.Intent == desc.Intent
            && request.Command == desc.Command;
    }

    AnimRequest* FindPrediction(AnimRequestSet& set, const AnimRequestDesc& desc)
    {
        for (AnimRequest& request : set.Records)
            if (request.Predicted && SameRequest(request, desc))
                return &request;
        return nullptr;
    }

    bool AuthorityIssued(const AnimRequestSet& set, const AnimRequestDesc& desc)
    {
        return std::ranges::any_of(set.Records, [&](const AnimRequest& request) {
            return !request.Predicted && SameRequest(request, desc);
        });
    }
}

AnimRequestResult AnimRequestJournal::Predict(World& world, EntityId animated, AnimRequestDesc desc, AnimTick now)
{
    if (Entries.size() >= kCapacity)
        return { AnimRequestStatus::Rejected, {}, AnimRejectReason::Capacity };
    desc.Predicted = true;
    const AnimRequestResult result = IssueAnimRequest(world, animated, desc, now);
    if (result.Status == AnimRequestStatus::Accepted || result.Status == AnimRequestStatus::Superseded)
        Entries.push_back(Entry{ .Animated = animated, .Desc = desc, .IssuedTick = now });
    return result;
}

void AnimRequestJournal::Reconcile(World& world, std::uint64_t acknowledgedCommand)
{
    std::erase_if(Entries, [&](const Entry& entry) {
        AnimRequestSet* set = SetOf(world, entry.Animated);
        if (set == nullptr)
            return true;
        // Undecided until the authority has run the tick it would have issued it on.
        if (entry.IssuedTick > acknowledgedCommand)
            return false;
        if (AnimRequest* prediction = FindPrediction(*set, entry.Desc))
            *prediction = AnimRequest{};
        // What played since rested on a guess the authority decided otherwise.
        if (!AuthorityIssued(*set, entry.Desc) && world.IsRegistered<AnimContentState>())
            if (AnimContentState* content = world.TryGet<AnimContentState>(entry.Animated))
                content->Reconstruct = true;
        return true;
    });

    for (const Entry& entry : Entries)
    {
        AnimRequestSet* set = SetOf(world, entry.Animated);
        if (set == nullptr || FindPrediction(*set, entry.Desc) != nullptr)
            continue;
        (void)IssueAnimRequest(world, entry.Animated, entry.Desc, entry.IssuedTick);
    }
}

AnimRequestResult RequestAnimation(World& world, EntityId animated, AnimRequestDesc desc, std::uint64_t localTick)
{
    const AnimTick now = SimulationTickOf(world, animated, localTick);
    if (desc.Command == 0)
        desc.Command = now;
    if (IsSimulationAuthority(world))
    {
        desc.Predicted = false;
        return IssueAnimRequest(world, animated, desc, now);
    }
    AnimRequestJournal* journal = world.TryGetResource<AnimRequestJournal>();
    if (!IsLocallyPredicted(world, animated) || journal == nullptr)
        return { AnimRequestStatus::Rejected, {}, AnimRejectReason::LeftToAuthority };
    return journal->Predict(world, animated, desc, now);
}

bool CancelAnimation(World& world, EntityId animated, AnimRequestId id, AnimCancelReason reason,
                     std::uint64_t localTick)
{
    if (!IsSimulationAuthority(world))
        return false;
    return CancelAnimRequest(world, animated, id, reason, SimulationTickOf(world, animated, localTick));
}
