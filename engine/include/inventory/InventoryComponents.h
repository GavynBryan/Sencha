#pragma once

#include <ecs/ComponentAnnotations.h>
#include <gameplay_tags/GameplayTagId.h>
#include <inventory/ItemDefinition.h>

#include <cstdint>
#include <type_traits>

struct SENCHA_COMPONENT("sencha.item_entry") SENCHA_AUTHORITY_MATERIALIZED ItemEntry
{
    ItemId Definition;
    std::uint32_t Count = 1;
};

struct SENCHA_COMPONENT("sencha.item_container") ItemContainer
{
    std::uint16_t Capacity = 0;
    GameplayTagId Accepts;
};

struct SENCHA_COMPONENT("sencha.item_pickup") ItemPickup
{
    ItemId Definition;
    std::uint32_t Count = 1;
};

static_assert(std::is_trivially_copyable_v<ItemEntry> && std::is_trivially_copyable_v<ItemContainer>
                  && std::is_trivially_copyable_v<ItemPickup>,
              "inventory components live in ECS chunks");

#if !defined(SENCHA_CODEGEN)
#  include <inventory/InventoryComponents.sencha.h>
#  include <inventory/ItemEntryWire.h>
#endif
