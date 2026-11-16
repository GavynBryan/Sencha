#pragma once

#include <abilities/AbilityGrants.h>
#include <attributes/AttributeId.h>
#include <core/identity/NamedDefinitionRegistry.h>
#include <core/identity/StrongId.h>
#include <effects/EffectId.h>
#include <gameplay_tags/GameplayTagId.h>

#include <cstdint>
#include <string>
#include <vector>

using ItemId = StrongId<struct ItemIdTag, std::uint32_t>;

struct ItemAttributeSeed
{
    AttributeId Attribute;
    float Base = 0.0f;
};

// docs/gameplay/inventory.md, "Item definitions".
struct ItemDefinition
{
    std::string Name{};
    std::vector<GameplayTagId> Tags{};
    std::uint32_t StackMax = 1;
    bool Keep = true;
    std::vector<EffectId> TakeEffects{};
    std::vector<ItemAttributeSeed> Attributes{};
    std::vector<GameplayTagId> InstanceTags{};
    // In the World's AbilityGrantEntries.
    std::uint16_t UseEntry = kAbilityNoGrantEntry;
};

class ItemRegistry final : public NamedDefinitionRegistry<ItemId, ItemDefinition>
{
};
