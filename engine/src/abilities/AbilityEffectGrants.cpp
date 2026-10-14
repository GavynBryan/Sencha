#include <abilities/AbilityEffectGrants.h>

#include <abilities/AbilityGrantEntries.h>
#include <abilities/AbilityGrants.h>
#include <abilities/AbilityRegistry.h>
#include <ecs/World.h>

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

void DeclareEffectGrants(World& world, EffectId effect, const std::vector<AbilityGrantDeclaration>& grants)
{
    AbilityRegistry* abilities = world.TryGetResource<AbilityRegistry>();
    AbilityGrantEntries* entries = world.TryGetResource<AbilityGrantEntries>();
    if (abilities == nullptr || entries == nullptr)
        return;
    std::vector<std::uint16_t> granted;
    for (const AbilityGrantDeclaration& grant : grants)
    {
        const std::uint16_t entry = entries->Intern({ grant.Key, abilities->Reserve(grant.Ability), grant.Override });
        if (entry != kAbilityNoGrantEntry)
            granted.push_back(entry);
    }
    AbilityEffectGrants& table = world.HasResource<AbilityEffectGrants>() ? world.GetResource<AbilityEffectGrants>()
                                                                          : world.AddResource<AbilityEffectGrants>();
    table.Set(effect, std::move(granted));
}
