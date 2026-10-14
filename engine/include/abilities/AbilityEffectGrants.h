#pragma once

#include <abilities/AbilityDeclaration.h>
#include <effects/EffectId.h>

#include <cstdint>
#include <span>
#include <vector>

class World;

// The grants each lasting effect gives its target while it lasts, by effect, as
// entries in the World's AbilityGrantEntries. The ability pass keeps every actor's
// effect-held grants in step with the effects on it.
class AbilityEffectGrants
{
public:
    void Set(EffectId effect, std::vector<std::uint16_t> entries);
    [[nodiscard]] std::span<const std::uint16_t> Of(EffectId effect) const;
    [[nodiscard]] bool Empty() const { return Granting == 0; }

private:
    std::vector<std::vector<std::uint16_t>> ByEffect;
    std::size_t Granting = 0;
};

// Records what `effect` grants, reserving every ability it names. Replaces what it
// granted before, so a reloaded effect's next application grants the new list.
void DeclareEffectGrants(World& world, EffectId effect, const std::vector<AbilityGrantDeclaration>& grants);
