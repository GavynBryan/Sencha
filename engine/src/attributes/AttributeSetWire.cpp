#include <attributes/AttributeSetWire.h>

#include <attributes/AttributeRegistry.h>
#include <ecs/World.h>

namespace
{
const AttributeRegistry* AttributesOf(const ReplicationWireContext& context)
{
    return context.Entities != nullptr ? context.Entities->TryGetResource<AttributeRegistry>() : nullptr;
}
}

void ReplicationCodec<AttributeSet>::ToWire(const ReplicationWireContext& context, const AttributeSet& set,
                                            AttributeSetWire& wire)
{
    wire = AttributeSetWire{};
    const AttributeRegistry* attributes = AttributesOf(context);
    if (attributes == nullptr)
        return;
    for (std::uint8_t i = 0; i < set.Count; ++i)
    {
        wire.Keys.Values[i] = attributes->WireKey(set.Ids[i]);
        wire.Base.Values[i] = set.Base[i];
        wire.Current.Values[i] = set.Current[i];
    }
}

void ReplicationCodec<AttributeSet>::FromWire(const ReplicationWireContext& context, const AttributeSetWire& wire,
                                              AttributeSet& set)
{
    const AttributeRegistry* attributes = AttributesOf(context);
    set = AttributeSet{};
    if (attributes == nullptr)
        return;
    for (std::size_t i = 0; i < AttributeSet::Capacity; ++i)
    {
        const AttributeId id = attributes->FindByWireKey(wire.Keys.Values[i]);
        if (!id.IsValid())
            continue;
        set.Ids[set.Count] = id;
        set.Base[set.Count] = wire.Base.Values[i];
        set.Current[set.Count] = wire.Current.Values[i];
        ++set.Count;
    }
}
