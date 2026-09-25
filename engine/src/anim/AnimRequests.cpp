#include <anim/AnimRequests.h>

#include <anim/AnimRig.h>
#include <anim/AnimRigBinding.h>
#include <ecs/World.h>

#include <algorithm>

namespace
{
    // The tick a Fixed or Impulse record stops being live. An Impulse is
    // observed for exactly the tick it starts on; a Fixed of zero ticks is the
    // same thing and is treated as one.
    AnimTick EndTick(const AnimRequest& request)
    {
        switch (request.Lifetime)
        {
        case AnimRequestLifetime::Held: return kAnimNoTick;
        case AnimRequestLifetime::Impulse: return request.StartTick + 1;
        case AnimRequestLifetime::Fixed:
            return request.StartTick + std::max<AnimTick>(request.FixedTicks, 1);
        }
        return kAnimNoTick;
    }

    void Log(AnimDecisionLog* log, AnimTick now, AnimDecisionCause cause, const AnimRequest& request,
             AnimCancelReason cancel = AnimCancelReason::None,
             AnimRejectReason reject = AnimRejectReason::None)
    {
        if (log == nullptr)
            return;
        AnimDecisionRecord record;
        record.Tick = now;
        record.Cause = cause;
        record.Request = request.Id;
        record.Intent = request.Intent;
        record.CancelReason = cancel;
        record.RejectReason = reject;
        log->Append(record);
    }

    // A component this World never registered is one no entity can carry.
    template <typename T>
    T* Find(World& world, EntityId entity)
    {
        return world.IsRegistered<T>() ? world.TryGet<T>(entity) : nullptr;
    }

    bool Newer(const AnimRequest& a, const AnimRequest& b)
    {
        if (a.StartTick != b.StartTick)
            return a.StartTick > b.StartTick;
        return a.Id.Sequence > b.Id.Sequence;
    }
}

bool IsAnimRequestLive(const AnimRequest& request, AnimTick now)
{
    return request.Occupied && !request.IsCancelled() && now >= request.StartTick
        && now < EndTick(request);
}

bool WasAnimRequestLive(const AnimRequest& request, AnimTick at)
{
    return request.Occupied && at >= request.StartTick && at < EndTick(request)
        && (!request.IsCancelled() || at < request.CancelTick);
}

bool IsAnimRequestRetained(const AnimRequest& request, AnimTick now)
{
    if (!request.Occupied)
        return false;
    if (request.IsCancelled())
        return now <= std::max(request.CancelTick, request.TailUntilTick);
    return now < EndTick(request);
}

void PruneAnimRequests(AnimRequestSet& set, AnimTick now, AnimDecisionLog* log)
{
    for (AnimRequest& request : set.Records)
    {
        if (!request.Occupied || IsAnimRequestRetained(request, now))
            continue;
        if (!request.IsCancelled())
            Log(log, now, AnimDecisionCause::RequestExpired, request);
        request = AnimRequest{};
    }
}

AnimRequestResult IssueAnimRequest(AnimRequestSet& set,
                                   const AnimRequestDesc& desc,
                                   AnimTick now,
                                   AnimDecisionLog* log)
{
    AnimRequest request;
    request.Intent = desc.Intent;
    request.SourceTag = desc.SourceTag;
    request.StartTick = now;
    request.FixedTicks = desc.FixedTicks;
    std::copy(desc.Params.begin(), desc.Params.end(), request.Params);
    request.TagParams = desc.TagParams;
    request.Layers = desc.Layers;
    request.Lifetime = desc.Lifetime;
    request.Occupied = true;
    request.Id.Source = desc.Source;

    if (!desc.Intent.IsValid() || !desc.Source.IsValid() || desc.Layers == 0)
    {
        Log(log, now, AnimDecisionCause::RequestRejected, request, AnimCancelReason::None,
            AnimRejectReason::Malformed);
        return { AnimRequestStatus::Rejected, {}, AnimRejectReason::Malformed };
    }

    PruneAnimRequests(set, now, log);

    if (desc.Lifetime == AnimRequestLifetime::Impulse)
    {
        for (const AnimRequest& existing : set.Records)
        {
            if (existing.Occupied && existing.Lifetime == AnimRequestLifetime::Impulse
                && existing.Id.Source == desc.Source && existing.Intent == desc.Intent
                && existing.StartTick == now)
            {
                Log(log, now, AnimDecisionCause::RequestDeduplicated, existing);
                return { AnimRequestStatus::Deduplicated, existing.Id, AnimRejectReason::None };
            }
        }
    }
    else
    {
        for (AnimRequest& existing : set.Records)
        {
            if (!IsAnimRequestLive(existing, now) || existing.Lifetime == AnimRequestLifetime::Impulse
                || !(existing.Id.Source == desc.Source) || existing.Intent != desc.Intent)
            {
                continue;
            }
            // In place, so a combo advancing never fails for capacity.
            Log(log, now, AnimDecisionCause::RequestSuperseded, existing,
                AnimCancelReason::Superseded);
            request.Id.Sequence = set.NextSequence++;
            existing = request;
            Log(log, now, AnimDecisionCause::RequestAdded, existing);
            return { AnimRequestStatus::Superseded, existing.Id, AnimRejectReason::None };
        }
    }

    for (AnimRequest& slot : set.Records)
    {
        if (slot.Occupied)
            continue;
        request.Id.Sequence = set.NextSequence++;
        slot = request;
        Log(log, now, AnimDecisionCause::RequestAdded, slot);
        return { AnimRequestStatus::Accepted, slot.Id, AnimRejectReason::None };
    }

    Log(log, now, AnimDecisionCause::RequestRejected, request, AnimCancelReason::None,
        AnimRejectReason::Capacity);
    return { AnimRequestStatus::Rejected, {}, AnimRejectReason::Capacity };
}

bool CancelAnimRequest(AnimRequestSet& set,
                       AnimRequestId id,
                       AnimCancelReason reason,
                       AnimTick now,
                       AnimDecisionLog* log)
{
    if (reason == AnimCancelReason::None)
        return false;
    for (AnimRequest& request : set.Records)
    {
        if (!(request.Id == id) || !IsAnimRequestLive(request, now))
            continue;
        request.CancelReason = reason;
        request.CancelTick = now;
        request.TailUntilTick = now;
        Log(log, now, AnimDecisionCause::RequestCancelled, request, reason);
        return true;
    }
    return false;
}

void ExtendAnimRequestTail(AnimRequestSet& set, AnimRequestId id, AnimTick tick)
{
    for (AnimRequest& request : set.Records)
    {
        if (request.Occupied && request.Id == id && request.IsCancelled())
            request.TailUntilTick = std::max(request.TailUntilTick, tick);
    }
}

const AnimRequest* FindPrimaryAnimRequest(const AnimRequestSet& set,
                                          GameplayTagId intent,
                                          AnimTick now,
                                          std::uint8_t layers)
{
    const AnimRequest* primary = nullptr;
    for (const AnimRequest& request : set.Records)
    {
        if (request.Intent != intent || (request.Layers & layers) == 0
            || !IsAnimRequestLive(request, now))
        {
            continue;
        }
        if (primary == nullptr || Newer(request, *primary))
            primary = &request;
    }
    return primary;
}

AnimCancelReason FindAnimCancelReason(const AnimRequestSet& set, GameplayTagId intent, AnimTick now)
{
    const AnimRequest* primary = nullptr;
    for (const AnimRequest& request : set.Records)
    {
        if (!request.Occupied || request.Intent != intent || !request.IsCancelled()
            || request.CancelTick != now)
        {
            continue;
        }
        if (primary == nullptr || Newer(request, *primary))
            primary = &request;
    }
    return primary != nullptr ? primary->CancelReason : AnimCancelReason::None;
}

AnimRequestResult IssueAnimRequest(World& world,
                                   EntityId animated,
                                   const AnimRequestDesc& desc,
                                   AnimTick now)
{
    AnimRequestSet* set = Find<AnimRequestSet>(world, animated);
    AnimDecisionLog* log = Find<AnimDecisionLog>(world, animated);
    if (set == nullptr)
        return { AnimRequestStatus::Rejected, {}, AnimRejectReason::Malformed };

    // An intent the rig's request schema does not declare has no params to
    // read and no rule written against it; refusing it here is what keeps a
    // misspelt intent from being silently unclaimed forever.
    AnimRequestDesc declared = desc;
    if (const AnimRig* rig = Find<AnimRig>(world, animated))
    {
        if (AnimRigBindings* bindings = world.TryGetResource<AnimRigBindings>())
        {
            const AnimBoundRig* bound = bindings->Resolve(rig->Rig, world);
            const AnimBoundIntent* intent = bound != nullptr ? bound->FindIntent(desc.Intent) : nullptr;
            if (intent != nullptr)
            {
                declared.TagParams = 0;
                for (std::size_t p = 0; p < intent->Params.size() && p < kAnimRequestParams; ++p)
                {
                    if (intent->Params[p].Kind == AnimRequestParamKind::Tag)
                        declared.TagParams |= static_cast<std::uint8_t>(1u << p);
                }
            }
            if (bound != nullptr && bound->HasRequestSchema && intent == nullptr)
            {
                AnimRequest rejected;
                rejected.Intent = desc.Intent;
                rejected.Id.Source = desc.Source;
                if (log != nullptr)
                {
                    AnimDecisionRecord record;
                    record.Tick = now;
                    record.Cause = AnimDecisionCause::RequestRejected;
                    record.Request = rejected.Id;
                    record.Intent = desc.Intent;
                    record.RejectReason = AnimRejectReason::UndeclaredIntent;
                    log->Append(record);
                }
                return { AnimRequestStatus::Rejected, {}, AnimRejectReason::UndeclaredIntent };
            }
        }
    }
    return IssueAnimRequest(*set, declared, now, log);
}

bool CancelAnimRequest(World& world,
                       EntityId animated,
                       AnimRequestId id,
                       AnimCancelReason reason,
                       AnimTick now)
{
    AnimRequestSet* set = Find<AnimRequestSet>(world, animated);
    if (set == nullptr)
        return false;
    AnimDecisionLog* log = Find<AnimDecisionLog>(world, animated);
    return CancelAnimRequest(*set, id, reason, now, log);
}
