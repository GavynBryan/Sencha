#include <anim/AnimRequestVerbs.h>

#include <anim/AnimPresentationRequests.h>
#include <anim/AnimRequestJournal.h>
#include <ecs/Query.h>
#include <ecs/World.h>
#include <time/SimClock.h>

#include <utility>
#include <vector>

namespace
{
    // Who asked: the participant behind the invocation, else whatever produced it.
    EntityId SourceOf(const VerbInvocation& invocation, EntityId target)
    {
        if (invocation.Instigator.IsValid())
            return invocation.Instigator;
        return invocation.Producer.IsValid() ? invocation.Producer : target;
    }
}

VerbAdmission AnimRequestOperations::Request(const VerbInvocation& invocation, EntityId target, GameplayTagId intent,
                                             AnimRequestLifetime lifetime, std::uint32_t ticks)
{
    AnimRequestDesc desc;
    desc.Intent = intent;
    desc.Lifetime = lifetime;
    desc.FixedTicks = ticks;
    desc.Source = SourceOf(invocation, target);
    desc.Cause = invocation.Id;
    const std::uint64_t tick = Clock->TimeOfTick(invocation.Tick).TickIndex;
    return RequestAnimation(*Entities, target, desc, tick).Accepted() ? VerbAdmission::Accepted
                                                                      : VerbAdmission::Refused;
}

VerbAdmission AnimRequestOperations::Cancel(const VerbInvocation& invocation, EntityId target, GameplayTagId intent)
{
    if (!Entities->IsRegistered<AnimRequestSet>())
        return VerbAdmission::InvalidArguments;
    const AnimRequestSet* set = static_cast<const World&>(*Entities).TryGet<AnimRequestSet>(target);
    if (set == nullptr)
        return VerbAdmission::InvalidArguments;
    const EntityId source = SourceOf(invocation, target);
    const std::uint64_t tick = Clock->TimeOfTick(invocation.Tick).TickIndex;
    for (const AnimRequest& request : set->Records)
    {
        if (!request.Occupied || request.IsCancelled() || request.Id.Source != source || request.Intent != intent)
            continue;
        return CancelAnimation(*Entities, target, request.Id, AnimCancelReason::Released, tick)
            ? VerbAdmission::Accepted
            : VerbAdmission::Refused;
    }
    return VerbAdmission::Refused;
}

VerbAdmission AnimRequestOperations::EndHold(const VerbInvocation& invocation)
{
    if (!Entities->IsRegistered<AnimRequestSet>())
        return VerbAdmission::Accepted;
    const std::uint64_t tick = Clock->TimeOfTick(invocation.Tick).TickIndex;
    std::vector<std::pair<EntityId, AnimRequestId>> held;
    Query<Read<AnimRequestSet>> sets(*Entities);
    sets.ForEachChunk([&](auto& view) {
        const auto records = view.template Read<AnimRequestSet>();
        const EntityIndex* indices = view.Entities();
        for (std::uint32_t i = 0; i < view.Count(); ++i)
            for (const AnimRequest& request : records[i].Records)
                if (request.Occupied && !request.IsCancelled() && request.Owner == invocation.Owner)
                    held.emplace_back(Entities->ResolveEntityIndex(indices[i]), request.Id);
    });
    for (const auto& [animated, id] : held)
        (void)CancelAnimation(*Entities, animated, id, AnimCancelReason::Released, tick);
    return VerbAdmission::Accepted;
}

VerbAdmission AnimRequestOperations::Hold(const VerbInvocation& invocation, EntityId target, GameplayTagId intent)
{
    AnimRequestDesc desc;
    desc.Intent = intent;
    desc.Lifetime = AnimRequestLifetime::Held;
    desc.Source = SourceOf(invocation, target);
    desc.Owner = invocation.Owner;
    desc.Cause = invocation.Id;
    const std::uint64_t tick = Clock->TimeOfTick(invocation.Tick).TickIndex;
    return RequestAnimation(*Entities, target, desc, tick).Accepted() ? VerbAdmission::Accepted
                                                                      : VerbAdmission::Refused;
}

VerbAdmission AnimRequestOperations::Present(const VerbInvocation& invocation, EntityId target, GameplayTagId intent,
                                             AnimPresentLifetime lifetime, std::uint32_t ticks)
{
    AnimPresentationRequests* presented =
        Entities->IsRegistered<AnimPresentationRequests>() ? Entities->TryGet<AnimPresentationRequests>(target) : nullptr;
    if (presented == nullptr)
        return VerbAdmission::Refused;
    AnimRequestDesc desc;
    desc.Intent = intent;
    desc.Lifetime = lifetime == AnimPresentLifetime::Fixed ? AnimRequestLifetime::Fixed : AnimRequestLifetime::Impulse;
    desc.FixedTicks = ticks;
    desc.Source = SourceOf(invocation, target);
    desc.Cause = invocation.Id;
    const std::uint64_t tick = Clock->TimeOfTick(invocation.Tick).TickIndex;
    return IssueAnimRequest(presented->Set, desc, tick, nullptr).Accepted() ? VerbAdmission::Accepted
                                                                            : VerbAdmission::Refused;
}
