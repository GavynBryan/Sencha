#include <world/held/Held.h>

#include <ecs/World.h>
#include <net/ReplicationWireContext.h>
#include <world/held/HeldRecordIndex.h>

void ComponentTraits<Held>::OnAdd(Held& held, World& world, EntityId record)
{
    HeldRecordIndex* index = world.TryGetResource<HeldRecordIndex>();
    if (index == nullptr)
        return;
    EntityId holder = held.Holder.Entity;
    PersistentEntityId awaited;
    if (!world.IsAlive(holder))
    {
        if (const EntityId resolved = ResolvePersistableRef(world, held.Holder); resolved.IsValid())
            holder = held.Holder.Entity = resolved;
        else if (held.Holder.Target.IsValid())
            awaited = held.Holder.Target;
    }
    index->Track(record, holder, awaited, held.Position);
    if (holder.IsValid() && !awaited.IsValid() && !world.IsAlive(holder))
        index->HolderDeparted(holder);
}

void ComponentTraits<Held>::OnRemove(const Held&, World& world, EntityId record)
{
    if (HeldRecordIndex* index = world.TryGetResource<HeldRecordIndex>())
        index->Untrack(record);
}

void ReplicationCodec<Held>::ToWire(const ReplicationWireContext& context, const Held& held, HeldWire& wire)
{
    wire = HeldWire{ .Holder = 0, .Position = held.Position };
    if (context.Entities != nullptr)
        wire.Holder = context.WireEntity(ResolvePersistableRef(*context.Entities, held.Holder));
}

void ReplicationCodec<Held>::FromWire(const ReplicationWireContext& context, const HeldWire& wire, Held& held)
{
    held.Holder = PersistableRef{ .Entity = context.LocalEntity(wire.Holder), .Target = {} };
    held.Position = static_cast<std::uint16_t>(wire.Position);
}
