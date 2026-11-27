#include <abilities/AbilityEffectGrants.h>

#include <abilities/AbilityGrantEntries.h>
#include <abilities/AbilityGrants.h>
#include <abilities/AbilityRegistry.h>
#include <ecs/World.h>
#include <effects/ActiveEffect.h>
#include <effects/EffectSystem.h>

#include <algorithm>
#include <format>

void AbilityEffectGrants::Set(EffectId effect, std::vector<std::uint16_t> entries)
{
    if (!effect.IsValid())
        return;
    if (effect.Value >= ByEffect.size())
        ByEffect.resize(effect.Value + 1);
    Granting -= ByEffect[effect.Value].empty() ? 0 : 1;
    Granting += entries.empty() ? 0 : 1;
    ByEffect[effect.Value] = std::move(entries);
    ++Declared;
}

std::span<const std::uint16_t> AbilityEffectGrants::Of(EffectId effect) const
{
    return effect.IsValid() && effect.Value < ByEffect.size() ? std::span<const std::uint16_t>(ByEffect[effect.Value])
                                                              : std::span<const std::uint16_t>{};
}

void AbilityEffectGrants::SetInstance(EntityId instance, std::vector<std::uint16_t> entries)
{
    const auto at = std::ranges::lower_bound(Instances, instance, {}, &Instance::Effect);
    if (at != Instances.end() && at->Effect == instance)
        at->Entries = std::move(entries);
    else
        Instances.insert(at, { instance, std::move(entries) });
}

std::span<const std::uint16_t> AbilityEffectGrants::Of(EntityId instance, EffectId effect) const
{
    const auto at = std::ranges::lower_bound(Instances, instance, {}, &Instance::Effect);
    return at != Instances.end() && at->Effect == instance ? std::span<const std::uint16_t>(at->Entries) : Of(effect);
}

void AbilityEffectGrants::ForgetEnded(const World& world)
{
    std::erase_if(Instances, [&](const Instance& instance) {
        return !world.IsAlive(instance.Effect) || !world.HasComponent<ActiveEffect>(instance.Effect);
    });
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
            refused.push_back(std::format("grant '{}' was refused: this World runs no abilities", grant.Key));
        return refused;
    }
    std::vector<std::uint16_t> granted;
    for (const AbilityGrantDeclaration& grant : grants)
    {
        AbilityGrantEntryRefusal refusal = AbilityGrantEntryRefusal::Full;
        const std::uint16_t entry = entries->Intern(
            { grant.Key, abilities->Reserve(grant.Ability), grant.Override, grant.Parameters }, &refusal);
        if (entry != kAbilityNoGrantEntry)
            granted.push_back(entry);
        else
            refused.push_back(std::format("grant '{}' {}", grant.Key, AbilityGrantEntryRefusalText(refusal)));
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

std::size_t AbilityGrantReservations::HeldFor(const World& world, EntityId target, std::span<const EntityId> ending) const
{
    std::size_t held = 0;
    for (const Reservation& reservation : Entries)
        if (reservation.Target == target && world.IsAlive(reservation.Effect)
            && std::ranges::find(ending, reservation.Effect) == ending.end())
            held += reservation.Count;
    return held;
}

void AbilityGrantReservations::Release(const World& world)
{
    std::erase_if(Entries, [&](const Reservation& reservation) {
        if (!world.IsAlive(reservation.Effect))
            return true;
        return std::ranges::any_of(AbilityGrantsOf(world, reservation.Target), [&](const AbilityGrant& grant) {
            return grant.Kind == AbilityGrantKind::Effect && grant.Holder == reservation.Effect;
        });
    });
}

std::size_t AbilityGrantRoom(const World& world, EntityId target)
{
    return AbilityGrantRoom(world, target, {});
}

std::size_t AbilityGrantRoom(const World& world, EntityId target, std::span<const EntityId> ending)
{
    const auto ends = [&](EntityId effect) {
        return !(world.IsAlive(effect) && world.HasComponent<ActiveEffect>(effect))
            || std::ranges::find(ending, effect) != ending.end();
    };
    std::size_t taken = 0;
    for (const AbilityGrant& grant : AbilityGrantsOf(world, target))
        taken += grant.Kind == AbilityGrantKind::Effect && ends(grant.Holder) ? 0 : 1;
    if (const AbilityGrantReservations* reservations = world.TryGetResource<AbilityGrantReservations>())
        taken += reservations->HeldFor(world, target, ending);
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
