#include <attributes/AttributeSetWire.h>

#include <attributes/AttributeRegistry.h>
#include <ecs/World.h>

#include <algorithm>

namespace
{
const AttributeRegistry* AttributesOf(const ReplicationWireContext& context)
{
    return context.Entities != nullptr ? context.Entities->TryGetResource<AttributeRegistry>() : nullptr;
}
}

void ReplicationCodec<AttributeSet>::ToWire(const ReplicationWireContext& context, const AttributeSet& set,
                                            ReplicationImage& wire)
{
    const AttributeRegistry* attributes = AttributesOf(context);
    if (attributes == nullptr)
        return;
    const std::span<AttributeWire> sent = wire.Resize<AttributeWire>(0, set.Count);
    for (std::size_t i = 0; i < sent.size(); ++i)
        sent[i] = AttributeWire{ .Key = attributes->WireKey(set.Ids[i]), .Base = set.Base[i], .Current = set.Current[i] };
    std::sort(sent.begin(), sent.end(), [](const AttributeWire& a, const AttributeWire& b) { return a.Key < b.Key; });
}

void ReplicationCodec<AttributeSet>::FromWire(const ReplicationWireContext& context, const ReplicationImage& wire,
                                              AttributeSet& set)
{
    const AttributeRegistry* attributes = AttributesOf(context);
    set = AttributeSet{};
    if (attributes == nullptr)
        return;
    for (const AttributeWire& sent : wire.List<AttributeWire>(0))
    {
        const AttributeId id = attributes->FindByWireKey(sent.Key);
        if (!id.IsValid())
            continue;
        (void)set.Add(id, sent.Base);
        *set.CurrentPtr(id) = sent.Current;
    }
}
