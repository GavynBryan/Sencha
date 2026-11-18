#include <equipment/WornGrantEntries.h>

#include <abilities/AbilityEffectGrants.h>
#include <abilities/AbilityGrants.h>
#include <ecs/World.h>
#include <equipment/EquipmentLayout.h>
#include <inventory/Inventory.h>
#include <inventory/ItemDefinition.h>

#include <algorithm>
#include <format>
#include <limits>

namespace
{
    void LayBindingOver(AbilityBindingDecl& binding, const AbilityBindingDecl& over)
    {
        const auto lay = [](auto& field, const auto& value) {
            if (value)
                field = value;
        };
        lay(binding.Action, over.Action);
        lay(binding.Policy, over.Policy);
        lay(binding.Priority, over.Priority);
        lay(binding.Consume, over.Consume);
        lay(binding.WhenBlocked, over.WhenBlocked);
        lay(binding.Context, over.Context);
        lay(binding.OnInterrupt, over.OnInterrupt);
    }

    struct WornGrantDeclarations
    {
        static constexpr std::uint64_t kNever = std::numeric_limits<std::uint64_t>::max();
        std::uint64_t Items = kNever;
        std::uint64_t Layouts = kNever;
        std::uint64_t Grants = kNever;
    };

    template <typename Registry, typename Fn>
    void ForEachDefined(const Registry& registry, Fn&& fn)
    {
        using Id = decltype(registry.Find(std::string_view{}));
        for (std::size_t i = 1; i <= registry.Size(); ++i)
        {
            const Id id{ static_cast<decltype(Id{}.Value)>(i) };
            if (const auto* definition = registry.Get(id))
                fn(id, *definition);
        }
    }
}

std::vector<AbilityGrantEntry> WornGrantEntries(const World& world, EffectId effect, const EquipmentSlot& slot,
                                                const ItemDefinition& item)
{
    std::vector<AbilityGrantEntry> worn;
    const AbilityEffectGrants* table = world.TryGetResource<AbilityEffectGrants>();
    const AbilityGrantEntries* entries = world.TryGetResource<AbilityGrantEntries>();
    if (table == nullptr || entries == nullptr)
        return worn;
    for (const std::uint16_t index : table->Of(effect))
    {
        AbilityGrantEntry entry = *entries->Find(index);
        LayBindingOver(entry.Override, slot.Binding);
        const auto own = std::ranges::find(item.GrantParameters, entry.Key, &ItemGrantParameters::Key);
        if (own != item.GrantParameters.end())
            for (const AbilityGrantParameter& parameter : own->Parameters)
            {
                const auto held = std::ranges::find(entry.Parameters, parameter.Key, &AbilityGrantParameter::Key);
                if (held != entry.Parameters.end())
                    held->Value = parameter.Value;
                else
                    entry.Parameters.push_back(parameter);
            }
        worn.push_back(std::move(entry));
    }
    return worn;
}

void DeclareWornGrantEntries(World& world, std::vector<std::string>& errors)
{
    const ItemRegistry* items = world.TryGetResource<ItemRegistry>();
    const EquipmentLayoutRegistry* layouts = world.TryGetResource<EquipmentLayoutRegistry>();
    const AbilityEffectGrants* table = world.TryGetResource<AbilityEffectGrants>();
    AbilityGrantEntries* entries = world.TryGetResource<AbilityGrantEntries>();
    if (items == nullptr || layouts == nullptr || table == nullptr || entries == nullptr)
        return;
    WornGrantDeclarations& last = world.HasResource<WornGrantDeclarations>() ? world.GetResource<WornGrantDeclarations>()
                                                                             : world.AddResource<WornGrantDeclarations>();
    const WornGrantDeclarations now{ .Items = items->Generation(), .Layouts = layouts->Generation(),
                                     .Grants = table->Generation() };
    if (last.Items == now.Items && last.Layouts == now.Layouts && last.Grants == now.Grants)
        return;
    last = now;

    ForEachDefined(*items, [&](ItemId itemId, const ItemDefinition& item) {
        if (item.Occupies.empty() || item.EquipEffects.empty())
            return;
        ForEachDefined(*layouts, [&](EquipmentLayoutId layoutId, const EquipmentLayout& layout) {
            for (const std::uint8_t index : EquipmentSlotsNamed(layout, item.Occupies.front()))
            {
                const EquipmentSlot& slot = layout.Slots[index];
                if (!ItemCarries(world, item, slot.Accepts))
                    continue;
                for (const ItemEquipEffect& equipped : item.EquipEffects)
                    for (AbilityGrantEntry& worn : WornGrantEntries(world, equipped.Effect, slot, item))
                    {
                        const std::string key = worn.Key;
                        if (entries->Intern(std::move(worn)) == kAbilityNoGrantEntry)
                            errors.push_back(std::format("{} worn in '{}' of {}: grant '{}' {}", items->GetName(itemId),
                                                         slot.Key, layouts->GetName(layoutId), key,
                                                         kAbilityGrantEntryRefusal));
                    }
            }
        });
    });
}

bool FindWornGrantEntries(const World& world, EffectId effect, const EquipmentSlot& slot, const ItemDefinition& item,
                          std::vector<std::uint16_t>& out)
{
    out.clear();
    const AbilityGrantEntries* entries = world.TryGetResource<AbilityGrantEntries>();
    for (const AbilityGrantEntry& worn : WornGrantEntries(world, effect, slot, item))
    {
        const std::uint16_t index = entries->IndexOf(worn);
        if (index == kAbilityNoGrantEntry)
            return false;
        out.push_back(index);
    }
    return true;
}
