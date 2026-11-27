#pragma once

#include <core/metadata/TypeSchema.h>
#include <inventory/InventoryComponents.h>
#include <net/ReplicationLayout.h>

#include <cstdint>

struct ItemEntryWire
{
    std::uint32_t Definition = 0;
    std::uint32_t Count = 1;
};

template <>
struct TypeSchema<ItemEntryWire>
{
    static constexpr std::string_view Name = "item_entry_wire";
    static auto Fields()
    {
        return std::tuple{ MakeField("definition", &ItemEntryWire::Definition),
                           MakeField("count", &ItemEntryWire::Count) };
    }
};

template <>
struct ReplicationCodec<ItemEntry>
{
    using Wire = ItemEntryWire;
    static void ToWire(const ReplicationWireContext& context, const ItemEntry& entry, ItemEntryWire& wire);
    static void FromWire(const ReplicationWireContext& context, const ItemEntryWire& wire, ItemEntry& entry);
};
