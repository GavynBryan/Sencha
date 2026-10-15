#include <abilities/AbilityLeases.h>

#include <authored/VerbDispatcher.h>

LeaseOwner AbilityLeaseOwner(EntityId actor, const AbilityRecord& record, std::uint8_t op)
{
    return LeaseOwner{ .Entity = actor, .Key = (record.Identity & ~std::uint64_t{ 0xFF }) | op };
}

void ReleaseAbilityLeases(VerbDispatcher* verbs, EntityId actor, AbilityRecord& record, std::uint64_t tick)
{
    const VerbInvocationSource source{ .Producer = actor, .Instigator = record.Instigator, .Tick = tick };
    while (record.LeaseCount > 0)
    {
        const AbilityHeldLease held = record.Leases[--record.LeaseCount];
        if (verbs != nullptr)
            (void)verbs->ReleaseLease(held.Lease, AbilityLeaseOwner(actor, record, held.Op), source);
    }
}
