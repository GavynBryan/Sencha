#include <framework/abilities/AbilitySet.h>

bool AbilitySet::Has(AbilityId id) const
{
    for (int i = 0; i < Count; ++i)
        if (Abilities[i] == id)
            return true;
    return false;
}

bool AbilitySet::Grant(AbilityId id)
{
    if (!id.IsValid() || Has(id) || Count >= Capacity)
        return false;
    Abilities[Count++] = id;
    return true;
}

bool AbilitySet::Revoke(AbilityId id)
{
    for (int i = 0; i < Count; ++i)
    {
        if (Abilities[i] == id)
        {
            Abilities[i] = Abilities[--Count]; // swap-remove; order not preserved
            return true;
        }
    }
    return false;
}
