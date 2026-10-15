#include <time/ActorTimeLeases.h>

#include <algorithm>
#include <array>

bool AcquireActorTimeLease(ActorTimeLeases& leases, const LeaseOwner& owner, std::int64_t scaleQ16,
                           std::uint64_t tick)
{
    if (leases.ActiveCount == kActorTimeLeaseCapacity)
        return false;
    leases.Active[leases.ActiveCount++] = ActorTimeLease{ owner, scaleQ16, tick + 1, kActorTimeOpen };
    return true;
}

void ReleaseActorTimeLeases(ActorTimeLeases& leases, const LeaseOwner& owner, std::uint64_t tick)
{
    for (std::uint8_t i = leases.ActiveCount; i-- > 0;)
    {
        if (leases.Active[i].Owner != owner)
            continue;
        ActorTimeLease ended = leases.Active[i];
        ended.UntilTick = tick + 1;
        std::copy(leases.Active + i + 1, leases.Active + leases.ActiveCount, leases.Active + i);
        leases.Active[--leases.ActiveCount] = ActorTimeLease{};
        // A lease that never took effect leaves nothing for a replay to apply.
        if (ended.UntilTick <= ended.FromTick)
            continue;
        if (leases.HistoryCount == kActorTimeHistoryCapacity)
        {
            ActorTimeLease* oldest = std::min_element(leases.History, leases.History + leases.HistoryCount,
                                                      [](const ActorTimeLease& a, const ActorTimeLease& b) {
                                                          return a.UntilTick < b.UntilTick;
                                                      });
            *oldest = leases.History[--leases.HistoryCount];
            ++leases.Evicted;
        }
        leases.History[leases.HistoryCount++] = ended;
    }
}

void PruneActorTimeHistory(ActorTimeLeases& leases, std::uint64_t tick)
{
    for (std::uint8_t i = leases.HistoryCount; i-- > 0;)
        if (leases.History[i].UntilTick + kActorTimeHistoryTicks <= tick)
            leases.History[i] = leases.History[--leases.HistoryCount];
}

std::int64_t ActorScaleAt(const ActorTimeLeases& leases, std::uint64_t tick, const ActorScaleLimits& limits)
{
    std::array<std::int64_t, kActorTimeLeaseCapacity + kActorTimeHistoryCapacity> scales{};
    std::size_t count = 0;
    const auto take = [&](const ActorTimeLease* first, std::uint8_t size) {
        for (const ActorTimeLease* lease = first; lease != first + size; ++lease)
            if (lease->FromTick <= tick && tick < lease->UntilTick)
                scales[count++] = lease->ScaleQ16;
    };
    take(leases.Active, leases.ActiveCount);
    take(leases.History, leases.HistoryCount);
    std::sort(scales.begin(), scales.begin() + static_cast<std::ptrdiff_t>(count));

    // Ascending, so once the product passes the hard maximum every factor left is
    // above one too: stopping there changes nothing the clamp would not, and keeps
    // the product inside 64 bits.
    std::int64_t scale = kActorTimeOne;
    for (std::size_t i = 0; i < count; ++i)
        scale = std::clamp<std::int64_t>((scale * scales[i] + kActorTimeOne / 2) >> 16, 1, kActorScaleHardMaximumQ16);
    const std::int64_t maximum = std::min(limits.MaximumQ16, kActorScaleHardMaximumQ16);
    return std::clamp(scale, std::min(limits.MinimumQ16, maximum), maximum);
}
