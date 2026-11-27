#include <time/ActorTimeVerbs.h>

#include <ecs/Query.h>
#include <ecs/World.h>
#include <time/ActorTime.h>
#include <time/ActorTimeLeases.h>
#include <time/SimClock.h>

#include <cmath>

VerbAdmission ActorTimeOperations::Scale(const VerbInvocation& invocation, EntityId target, float scale)
{
    if (!Entities->IsRegistered<ActorTime>() || !Entities->HasComponent<ActorTime>(target)
        || !Entities->IsRegistered<ActorTimeLeases>())
        return VerbAdmission::Refused;
    if (!Entities->HasComponent<ActorTimeLeases>(target))
        Entities->AddComponent<ActorTimeLeases>(target, ActorTimeLeases{});
    const std::uint64_t tick = Clock->TimeOfTick(invocation.Tick).TickIndex;
    const std::int64_t scaleQ16 = std::llround(static_cast<double>(scale) * static_cast<double>(kActorTimeOne));
    return AcquireActorTimeLease(*Entities->TryGet<ActorTimeLeases>(target), invocation.Owner, scaleQ16, tick)
        ? VerbAdmission::Accepted
        : VerbAdmission::Refused;
}

VerbAdmission ActorTimeOperations::EndScale(const VerbInvocation& invocation)
{
    if (!Entities->IsRegistered<ActorTimeLeases>())
        return VerbAdmission::Accepted;
    const std::uint64_t tick = Clock->TimeOfTick(invocation.Tick).TickIndex;
    Query<Write<ActorTimeLeases>> held(*Entities);
    held.ForEachChunk([&](auto& view) {
        auto leases = view.template Write<ActorTimeLeases>();
        for (std::uint32_t i = 0; i < view.Count(); ++i)
            ReleaseActorTimeLeases(leases[i], invocation.Owner, tick);
    });
    return VerbAdmission::Accepted;
}
