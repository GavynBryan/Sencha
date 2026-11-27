#pragma once

#include <abilities/AbilityCostProviders.h>

#include <span>
#include <string_view>

class World;

// docs/gameplay/inventory.md, "Spending, depletion and the inventory pass".
inline constexpr std::string_view kItemCostProvider = "inventory.item";

[[nodiscard]] AbilityCostProviderCheck CheckItemCosts(const World& world, std::span<const AbilityCostRow> rows);
void ApplyItemCosts(World& world, std::span<const AbilityCostRow> rows);

void DeclareItemCostProvider(World& world);
