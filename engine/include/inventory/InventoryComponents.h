#pragma once

#include <core/metadata/Field.h>
#include <core/metadata/TypeSchema.h>
#include <ecs/ComponentAnnotations.h>
#include <gameplay_tags/GameplayTagId.h>
#include <inventory/ItemDefinition.h>

#include <cstdint>
#include <type_traits>

// Its scene form is hand-written (InventorySerializers.h): a loaded count its stack cannot hold is clamped.
struct SENCHA_COMPONENT("sencha.item_entry")
       SENCHA_SCHEMA("ItemEntry")
       SENCHA_REPLICATED
       SENCHA_AUTHORITY_MATERIALIZED
       SENCHA_SAVED
ItemEntry
{
    SENCHA_FIELD("definition")
    SENCHA_OPTIONAL
    SENCHA_LABEL("Item")
    ItemId Definition;

    SENCHA_FIELD("count")
    SENCHA_LABEL("Count")
    std::uint32_t Count = 1;
};

struct SENCHA_COMPONENT("sencha.item_container")
       SENCHA_SCHEMA("ItemContainer")
       SENCHA_SCENE_CHUNK("ITCN")
       SENCHA_SAVED
ItemContainer
{
    SENCHA_FIELD("capacity")
    SENCHA_LABEL("Holds")
    SENCHA_TOOLTIP("Entries it holds at most; zero is as many as a capacity can say.")
    std::uint16_t Capacity = 0;

    SENCHA_FIELD("accepts")
    SENCHA_OPTIONAL
    SENCHA_LABEL("Accepts")
    SENCHA_TOOLTIP("An item must carry this tag, or one beneath it. None accepts anything.")
    GameplayTagId Accepts;
};

struct SENCHA_COMPONENT("sencha.item_pickup")
       SENCHA_SCHEMA("ItemPickup")
       SENCHA_SCENE_CHUNK("ITPK")
       SENCHA_SAVED
ItemPickup
{
    SENCHA_FIELD("definition")
    SENCHA_OPTIONAL
    SENCHA_LABEL("Item")
    SENCHA_TOOLTIP("Which item this is, by the asset that declares it.")
    ItemId Definition;

    SENCHA_FIELD("count")
    SENCHA_LABEL("Count")
    SENCHA_TOOLTIP("How many units taking it gives, when it holds no entry of its own.")
    std::uint32_t Count = 1;
};

static_assert(std::is_trivially_copyable_v<ItemEntry> && std::is_trivially_copyable_v<ItemContainer>
                  && std::is_trivially_copyable_v<ItemPickup>,
              "inventory components live in ECS chunks");

#if !defined(SENCHA_CODEGEN)
#  include <inventory/InventoryComponents.sencha.h>
#endif
