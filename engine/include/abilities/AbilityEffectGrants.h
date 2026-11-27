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

    // One applied instance's grants in place of its definition's, entry for entry:
    // an equipped item's slot binding and parameters. Kept until it ends.
    void SetInstance(EntityId instance, std::vector<std::uint16_t> entries);
    [[nodiscard]] std::span<const std::uint16_t> Of(EntityId instance, EffectId effect) const;
    void ForgetEnded(const World& world);

    [[nodiscard]] bool Empty() const { return Granting == 0 && Instances.empty(); }
    [[nodiscard]] std::uint64_t Generation() const { return Declared; }
    [[nodiscard]] std::size_t InstanceCount() const { return Instances.size(); }

private:
    struct Instance
    {
        EntityId Effect;
        std::vector<std::uint16_t> Entries;
    };

    std::vector<std::vector<std::uint16_t>> ByEffect;
    std::size_t Granting = 0;
    std::uint64_t Declared = 0;
    // Sorted by instance.
    std::vector<Instance> Instances;
};

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
    [[nodiscard]] std::size_t HeldFor(const World& world, EntityId target, std::span<const EntityId> ending = {}) const;
    void Release(const World& world);

    [[nodiscard]] std::size_t Size() const { return Entries.size(); }

private:
    std::vector<Reservation> Entries;
};

[[nodiscard]] std::size_t AbilityGrantRoom(const World& world, EntityId target);
// As though `ending` effects had already ended.
[[nodiscard]] std::size_t AbilityGrantRoom(const World& world, EntityId target, std::span<const EntityId> ending);

void InstallAbilityGrantAdmission(World& world);

// Records what `effect` grants, reserving every ability it names. Replaces what it
// granted before, so a reloaded effect's next application grants the new list.
// Returns a message for each grant it refused.
std::vector<std::string> DeclareEffectGrants(World& world, EffectId effect,
                                             const std::vector<AbilityGrantDeclaration>& grants);
