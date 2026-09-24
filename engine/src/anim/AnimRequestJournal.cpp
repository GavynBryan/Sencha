#include <anim/AnimRequestJournal.h>

#include <ecs/World.h>
#include <world/SimulationAuthority.h>

#include <algorithm>

namespace
{
    AnimRequestSet* SetOf(World& world, EntityId animated)
    {
        return world.IsAlive(animated) && world.IsRegistered<AnimRequestSet>() ? world.TryGet<AnimRequestSet>(animated)
                                                                                : nullptr;
    }

    // The prediction `desc` made at `tick`, while it is still on the set.
    AnimRequest* FindPrediction(AnimRequestSet& set, const AnimRequestDesc& desc, AnimTick tick)
    {
        for (AnimRequest& request : set.Records)
            if (request.Occupied && request.Predicted && request.Id.Source == desc.Source
                && request.Intent == desc.Intent && request.StartTick == tick)
                return &request;
        return nullptr;
    }

    void MarkPredicted(World& world, EntityId animated, const AnimRequestResult& result)
    {
        if (!result.Accepted())
            return;
        if (AnimRequestSet* set = SetOf(world, animated))
            for (AnimRequest& request : set->Records)
                if (request.Occupied && request.Id == result.Id)
                    request.Predicted = true;
    }
}

AnimRequestResult AnimRequestJournal::Issue(World& world, EntityId animated, const AnimRequestDesc& desc, AnimTick now,
                                            std::uint64_t commandTick)
{
    const AnimRequestResult result = IssueAnimRequest(world, animated, desc, now);
    if (IsSimulationAuthority(world) || !result.Accepted())
        return result;
    MarkPredicted(world, animated, result);
    if (Entries.size() < kCapacity)
        Entries.push_back(Entry{ .Animated = animated, .Desc = desc, .IssuedTick = now, .CommandTick = commandTick });
    return result;
}

void AnimRequestJournal::Reconcile(World& world, std::uint64_t acknowledgedCommand)
{
    std::erase_if(Entries, [&](const Entry& entry) {
        AnimRequestSet* set = SetOf(world, entry.Animated);
        if (set == nullptr)
            return true;
        if (entry.CommandTick > acknowledgedCommand)
            return false;
        // Decided. A snapshot that carried the set already replaced the
        // guess; one that did not left it standing, and nothing else would
        // ever take it down.
        if (AnimRequest* guess = FindPrediction(*set, entry.Desc, entry.IssuedTick))
            *guess = AnimRequest{};
        return true;
    });

    for (const Entry& entry : Entries)
    {
        AnimRequestSet* set = SetOf(world, entry.Animated);
        if (set == nullptr || FindPrediction(*set, entry.Desc, entry.IssuedTick) != nullptr)
            continue;
        MarkPredicted(world, entry.Animated, IssueAnimRequest(world, entry.Animated, entry.Desc, entry.IssuedTick));
    }
}
