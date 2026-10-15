#include <abilities/ActiveAbilities.h>

#include <core/hash/Fnv1a.h>

std::string_view AbilityEndReasonName(AbilityEndReason reason)
{
    switch (reason)
    {
    case AbilityEndReason::None: return "none";
    case AbilityEndReason::Completed: return "completed";
    case AbilityEndReason::InputReleased: return "input released";
    case AbilityEndReason::InputInterrupted: return "input interrupted";
    case AbilityEndReason::InputLost: return "input lost";
    case AbilityEndReason::GrantRevoked: return "grant revoked";
    case AbilityEndReason::ContinuationFailed: return "continuation failed";
    case AbilityEndReason::CancelTriggered: return "cancel triggered";
    case AbilityEndReason::CancelledByAbility: return "cancelled by an ability";
    case AbilityEndReason::TargetLost: return "target lost";
    case AbilityEndReason::CommitFailed: return "commit failed";
    case AbilityEndReason::LeaseRefused: return "lease refused";
    case AbilityEndReason::ActorEnded: return "actor ended";
    case AbilityEndReason::ZoneDetached: return "zone detached";
    case AbilityEndReason::AuthorityDenied: return "authority denied";
    case AbilityEndReason::HotReload: return "hot reload";
    case AbilityEndReason::ExplicitCancel: return "cancelled";
    case AbilityEndReason::StepBudget: return "step budget";
    case AbilityEndReason::CapacityExceeded: return "capacity exceeded";
    }
    return "unknown";
}

std::uint64_t AbilityActivationIdentity(const AbilityActivationKey& key, std::uint32_t abilityWireKey,
                                        AbilityGrantId grant)
{
    std::uint64_t hash = kFnv1aOffsetBasis;
    HashFnv1aValue(hash, key.CommandTick);
    HashFnv1aValue(hash, abilityWireKey);
    HashFnv1aValue(hash, key.Origin);
    HashFnv1aValue(hash, grant.Value);
    return hash == 0 ? 1u : hash;
}

bool IsAbilityRecordShadowed(const ActiveAbilities& active, std::size_t slot)
{
    const AbilityRecord& record = active.Records[slot];
    if (record.State == AbilityRecordState::Free || record.Predicted)
        return false;
    for (const AbilityRecord& other : active.Records)
        if (other.Predicted && other.State != AbilityRecordState::Free && other.Identity == record.Identity)
            return true;
    return false;
}
