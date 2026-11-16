#include <inventory/ItemEntryWire.h>

#include <ecs/World.h>
#include <net/ReplicationWireContext.h>

void ReplicationCodec<ItemEntry>::ToWire(const ReplicationWireContext& context, const ItemEntry& entry,
                                         ItemEntryWire& wire)
{
    const ItemRegistry* items = context.Entities != nullptr ? context.Entities->TryGetResource<ItemRegistry>() : nullptr;
    wire = ItemEntryWire{ .Definition = items != nullptr ? items->WireKey(entry.Definition) : 0u, .Count = entry.Count };
}

void ReplicationCodec<ItemEntry>::FromWire(const ReplicationWireContext& context, const ItemEntryWire& wire,
                                           ItemEntry& entry)
{
    const ItemRegistry* items = context.Entities != nullptr ? context.Entities->TryGetResource<ItemRegistry>() : nullptr;
    entry = ItemEntry{ .Definition = items != nullptr ? items->FindByWireKey(wire.Definition) : ItemId{},
                       .Count = wire.Count };
}
