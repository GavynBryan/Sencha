#pragma once

#include <abilities/AbilityDeclaration.h>
#include <ecs/EntityId.h>
#include <effects/EffectId.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
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

// Room an applied effect's grants hold in its target's grant table until the pass
// grants them, so an effect admitted is an effect whose grants fit
// (docs/gameplay/abilitykit.md, "Grants and input"). A World resource.
class AbilityGrantReservations
{
public:
    struct Reservation
    {
        EntityId Target;
        EntityId Effect;
        std::uint8_t Count = 0;
    };

    void Reserve(EntityId target, EntityId effect, std::uint8_t count);
    // Room held for `target` by effects still alive.
    [[nodiscard]] std::size_t HeldFor(const World& world, EntityId target) const;
    // Drops every reservation whose effect is gone or whose grants its target holds.
    void Release(const World& world);

    [[nodiscard]] std::size_t Size() const { return Entries.size(); }

private:
    std::vector<Reservation> Entries;
};

// The grants `target` could still be given: its table's capacity less what live
// grants and outstanding reservations hold. An effect's grant whose effect has
// ended counts as free, since the pass revokes it before it grants.
[[nodiscard]] std::size_t AbilityGrantRoom(const World& world, EntityId target);

// Admits a lasting effect only when its grants fit, and reserves them; what
// RegisterAbilityKit installs as the World's EffectApplicationAdmission.
void InstallAbilityGrantAdmission(World& world);

// Records what `effect` grants, reserving every ability it names. Replaces what it
// granted before, so a reloaded effect's next application grants the new list.
// Returns the keys the entry table refused, which the effect does not grant.
std::vector<std::string> DeclareEffectGrants(World& world, EffectId effect,
                                             const std::vector<AbilityGrantDeclaration>& grants);
