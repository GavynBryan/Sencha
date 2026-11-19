#pragma once

#include <abilities/AbilityCostProviders.h>
#include <ecs/World.h>

#include <cstdint>
#include <map>
#include <span>

struct Counters
{
    std::map<EntityId, std::int64_t> Held;
    mutable std::size_t Checks = 0;
};

inline AbilityCostProviderCheck CheckCounters(const World& world, std::span<const AbilityCostRow> rows)
{
    const Counters& counters = *world.TryGetResource<Counters>();
    ++counters.Checks;
    std::map<EntityId, std::int64_t> needed;
    for (std::uint32_t r = 0; r < rows.size(); ++r)
    {
        EntityId holder;
        std::int64_t count = 0;
        if (!rows[r].Arguments[0].TryGetEntity(holder) || !rows[r].Arguments[1].TryGetInt(count))
            return { .Affordable = false, .Row = r };
        needed[holder] += count;
        const auto held = counters.Held.find(holder);
        if (held == counters.Held.end() || held->second < needed[holder])
            return { .Affordable = false, .Row = r };
    }
    return {};
}

inline void ApplyCounters(World& world, std::span<const AbilityCostRow> rows)
{
    Counters& counters = world.GetResource<Counters>();
    for (const AbilityCostRow& row : rows)
    {
        EntityId holder;
        std::int64_t count = 0;
        (void)row.Arguments[0].TryGetEntity(holder);
        (void)row.Arguments[1].TryGetInt(count);
        counters.Held[holder] -= count;
    }
}

inline bool DeclareCounterCostProvider(World& world)
{
    world.AddResource<Counters>();
    AbilityCostProviderDefinition counter;
    counter.Name = "test.counter";
    DataFieldSchema holder = MakeDataField(DataFieldKind::Entity, "holder", "Holder");
    DataFieldSchema count = MakeDataField(DataFieldKind::Int, "count", "Count");
    count.Default = std::int64_t{ 1 };
    count.Required = false;
    counter.Arguments.Children = { std::move(holder), std::move(count) };
    counter.Check = &CheckCounters;
    counter.Apply = &ApplyCounters;
    AbilityCostProviderRegistrationScope scope(world.GetResource<AbilityCostProviderCatalog>(), "test");
    return scope.Declare(std::move(counter)) && scope.Commit();
}
