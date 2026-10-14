#include <abilities/AbilityGrants.h>

#include <algorithm>

const AbilityGrant* AbilityGrants::Find(AbilityGrantId id) const
{
    const auto found = std::find_if(Entries, Entries + Count, [id](const AbilityGrant& grant) { return grant.Id == id; });
    return found != Entries + Count ? found : nullptr;
}

AbilityGrantId GrantAbility(AbilityGrants& grants, AbilityId ability, AbilityGrantKind kind, std::uint16_t entry,
                            EntityId effect)
{
    if (!ability.IsValid() || grants.Count == kAbilityGrantCapacity)
        return AbilityGrantId{};
    const AbilityGrantId id{ grants.NextId++ };
    grants.Entries[grants.Count++] = AbilityGrant{ id, ability, kind, effect, entry };
    return id;
}

bool RevokeAbilityGrant(AbilityGrants& grants, AbilityGrantId id)
{
    AbilityGrant* const end = grants.Entries + grants.Count;
    AbilityGrant* const found = std::find_if(grants.Entries, end, [id](const AbilityGrant& grant) { return grant.Id == id; });
    if (found == end)
        return false;
    std::copy(found + 1, end, found);
    --grants.Count;
    grants.Entries[grants.Count] = AbilityGrant{};
    return true;
}
