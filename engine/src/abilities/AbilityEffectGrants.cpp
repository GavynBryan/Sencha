#include <abilities/AbilityEffectGrants.h>

#include <abilities/AbilityGrantEntries.h>
#include <abilities/AbilityGrants.h>
#include <abilities/AbilityRegistry.h>
#include <ecs/World.h>
#include <effects/ActiveEffect.h>
#include <effects/EffectSystem.h>

#include <algorithm>

void AbilityEffectGrants::Set(EffectId effect, std::vector<std::uint16_t> entries)
{
    if (!effect.IsValid())
        return;
    if (effect.Value >= ByEffect.size())
        ByEffect.resize(effect.Value + 1);
    Granting -= ByEffect[effect.Value].empty() ? 0 : 1;
    Granting += entries.empty() ? 0 : 1;
    ByEffect[effect.Value] = std::move(entries);
}

std::span<const std::uint16_t> AbilityEffectGrants::Of(EffectId effect) const
{
    return effect.IsValid() && effect.Value < ByEffect.size() ? std::span<const std::uint16_t>(ByEffect[effect.Value])
                                                              : std::span<const std::uint16_t>{};
}

std::vector<std::string> DeclareEffectGrants(World& world, EffectId effect,
                                             const std::vector<AbilityGrantDeclaration>& grants)
{
    std::vector<std::string> refused;
    AbilityRegistry* abilities = world.TryGetResource<AbilityRegistry>();
    AbilityGrantEntries* entries = world.TryGetResource<AbilityGrantEntries>();
    if (abilities == nullptr || entries == nullptr)
    {
        for (const AbilityGrantDeclaration& grant : grants)
            refused.push_back(grant.Key);
        return refused;
    }
    std::vector<std::uint16_t> granted;
    for (const AbilityGrantDeclaration& grant : grants)
    {
        const std::uint16_t entry =
            entries->Intern({ grant.Key, abilities->Reserve(grant.Ability), grant.Override, grant.Parameters });
        if (entry != kAbilityNoGrantEntry)
            granted.push_back(entry);
        else
            refused.push_back(grant.Key);
    }
    AbilityEffectGrants& table = world.HasResource<AbilityEffectGrants>() ? world.GetResource<AbilityEffectGrants>()
                                                                          : world.AddResource<AbilityEffectGrants>();
    table.Set(effect, std::move(granted));
    return refused;
}

void AbilityGrantReservations::Reserve(EntityId target, EntityId effect, std::uint8_t count)
{
    Entries.push_back({ target, effect, count });
}

std::size_t AbilityGrantReservations::HeldFor(const World& world, EntityId target) const
{
    std::size_t held = 0;
    for (const Reservation& reservation : Entries)
        if (reservation.Target == target && world.IsAlive(reservation.Effect))
            held += reservation.Count;
    return held;
}

void AbilityGrantReservations::Release(const World& world)
{
    std::erase_if(Entries, [&](const Reservation& reservation) {
        if (!world.IsAlive(reservation.Effect))
            return true;
        const AbilityGrants* grants = world.IsAlive(reservation.Target) ? world.TryGet<AbilityGrants>(reservation.Target)
                                                                         : nullptr;
        return grants != nullptr && std::any_of(grants->Entries, grants->Entries + grants->Count, [&](const AbilityGrant& grant) {
                   return grant.Kind == AbilityGrantKind::Effect && grant.Holder == reservation.Effect;
               });
    });
}

std::size_t AbilityGrantRoom(const World& world, EntityId target)
{
    std::size_t taken = 0;
    if (const AbilityGrants* grants = world.IsRegistered<AbilityGrants>() ? world.TryGet<AbilityGrants>(target) : nullptr)
        for (std::uint8_t i = 0; i < grants->Count; ++i)
        {
            const AbilityGrant& grant = grants->Entries[i];
            const bool ended = grant.Kind == AbilityGrantKind::Effect
                && !(world.IsAlive(grant.Holder) && world.HasComponent<ActiveEffect>(grant.Holder));
            taken += ended ? 0 : 1;
        }
    if (const AbilityGrantReservations* reservations = world.TryGetResource<AbilityGrantReservations>())
        taken += reservations->HeldFor(world, target);
    return taken >= kAbilityGrantCapacity ? 0 : kAbilityGrantCapacity - taken;
}

namespace
{
    std::size_t GrantsOf(const World& world, EffectId effect)
    {
        const AbilityEffectGrants* table = world.TryGetResource<AbilityEffectGrants>();
        return table != nullptr ? table->Of(effect).size() : 0;
    }

    bool AdmitGrantingEffect(World& world, EntityId target, EffectId effect)
    {
        const std::size_t count = GrantsOf(world, effect);
        return count == 0 || count <= AbilityGrantRoom(world, target);
    }

    void ReserveEffectGrants(World& world, EntityId target, EffectId effect, EntityId instance)
    {
        if (const std::size_t count = GrantsOf(world, effect); count > 0)
            world.GetResource<AbilityGrantReservations>().Reserve(target, instance, static_cast<std::uint8_t>(count));
    }
}

void InstallAbilityGrantAdmission(World& world)
{
    if (!world.HasResource<AbilityGrantReservations>())
        world.AddResource<AbilityGrantReservations>();
    world.SetResource(EffectApplicationAdmission{ .Admit = &AdmitGrantingEffect, .Applied = &ReserveEffectGrants });
}
