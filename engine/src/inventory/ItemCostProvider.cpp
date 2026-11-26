#include <inventory/ItemCostProvider.h>

#include <ecs/World.h>
#include <inventory/Inventory.h>
#include <inventory/InventoryComponents.h>
#include <inventory/InventorySystem.h>

#include <algorithm>
#include <cassert>
#include <limits>
#include <utility>
#include <vector>

namespace
{
    EntityId SpentEntry(const World& world, const AbilityCostRow& row)
    {
        EntityId entry;
        if (row.Arguments.size() > 0 && row.Arguments[0].TryGetEntity(entry) && world.IsAlive(entry))
            return world.HasComponent<ItemEntry>(entry) && MayUseItem(world, row.Actor, entry) ? entry : EntityId{};
        EntityId container;
        if (row.Arguments.size() < 2 || !row.Arguments[1].TryGetEntity(container)
            || !MayUseItem(world, row.Actor, container))
            return EntityId{};
        return FindItemEntry(world, container, ItemFilter{});
    }

    std::uint32_t UnitsOf(const AbilityCostRow& row)
    {
        std::int64_t count = 1;
        if (row.Arguments.size() > 2)
            (void)row.Arguments[2].TryGetInt(count);
        return static_cast<std::uint32_t>(std::clamp<std::int64_t>(count, 0, std::numeric_limits<std::uint32_t>::max()));
    }
}

AbilityCostProviderCheck CheckItemCosts(const World& world, std::span<const AbilityCostRow> rows)
{
    std::vector<std::pair<EntityId, std::uint64_t>> spent;
    for (std::uint32_t r = 0; r < rows.size(); ++r)
    {
        const EntityId entry = SpentEntry(world, rows[r]);
        if (!entry.IsValid())
            return { .Affordable = false, .Row = r };
        auto found = std::ranges::find(spent, entry, &std::pair<EntityId, std::uint64_t>::first);
        if (found == spent.end())
            found = spent.insert(spent.end(), { entry, 0u });
        found->second += UnitsOf(rows[r]);
        if (found->second > world.TryGet<ItemEntry>(entry)->Count)
            return { .Affordable = false, .Row = r };
    }
    return {};
}

void ApplyItemCosts(World& world, std::span<const AbilityCostRow> rows)
{
    assert(CheckItemCosts(world, rows).Affordable && "an item cost applied without a passing check");
    for (const AbilityCostRow& row : rows)
    {
        const EntityId entry = SpentEntry(world, row);
        const std::uint32_t held = std::as_const(world).TryGet<ItemEntry>(entry)->Count;
        SetItemCount(world, entry, held - std::min(held, UnitsOf(row)), row.Actor);
    }
}

void DeclareItemCostProvider(World& world)
{
    AbilityCostProviderCatalog* catalog = world.TryGetResource<AbilityCostProviderCatalog>();
    if (catalog == nullptr || catalog->Find(kItemCostProvider).IsValid())
        return;
    AbilityCostProviderDefinition item;
    item.Name = std::string(kItemCostProvider);
    item.DisplayName = "Items";
    item.Description = "Units spent from an entry, or from the first a container holds.";
    DataFieldSchema entry = MakeDataField(DataFieldKind::Entity, "entry", "Entry", "The entry spent from.");
    entry.Required = false;
    DataFieldSchema container = MakeDataField(DataFieldKind::Entity, "container", "Container",
                                              "Spends from the first entry it holds that still has units.");
    container.Required = false;
    DataFieldSchema count = MakeDataField(DataFieldKind::Int, "count", "Count");
    count.Default = std::int64_t{ 1 };
    count.Required = false;
    item.Arguments.Children = { std::move(entry), std::move(container), std::move(count) };
    item.Check = &CheckItemCosts;
    item.Apply = &ApplyItemCosts;
    AbilityCostProviderRegistrationScope scope(*catalog, "inventory");
    (void)scope.Declare(std::move(item));
    (void)scope.Commit();
}
