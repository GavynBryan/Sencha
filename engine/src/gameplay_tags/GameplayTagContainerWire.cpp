#include <gameplay_tags/GameplayTagContainerWire.h>

#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <gameplay_tags/GameplayTagWire.h>

namespace
{
const GameplayTagRegistry* TagsOf(const ReplicationWireContext& context)
{
    return context.Entities != nullptr ? context.Entities->TryGetResource<GameplayTagRegistry>() : nullptr;
}
}

void ReplicationCodec<GameplayTagContainer>::ToWire(const ReplicationWireContext& context,
                                                    const GameplayTagContainer& tags, GameplayTagContainerWire& wire)
{
    wire = GameplayTagContainerWire{};
    const GameplayTagRegistry* registry = TagsOf(context);
    std::size_t owned = 0;
    std::size_t shared = 0;
    for (std::uint8_t i = 0; i < tags.Count; ++i)
    {
        const std::uint32_t key = WireTag(context, tags.Tags[i]);
        if (key == 0)
            continue;
        if (registry != nullptr && registry->IsReplicatedToAll(tags.Tags[i]))
        {
            if (shared < kGameplayTagSharedCapacity)
                wire.Shared.Keys[shared++] = key;
            continue;
        }
        wire.Owned.Keys[owned] = key;
        wire.Owned.Counts[owned++] = tags.Counts[i];
    }
}

void ReplicationCodec<GameplayTagContainer>::FromWire(const ReplicationWireContext& context,
                                                      const GameplayTagContainerWire& wire, GameplayTagContainer& tags)
{
    tags.Clear();
    for (std::size_t i = 0; i < GameplayTagContainer::Capacity; ++i)
        if (const GameplayTagId tag = LocalTag(context, wire.Owned.Keys[i]); tag.IsValid() && wire.Owned.Counts[i] > 0)
            (void)tags.Grant(tag, static_cast<std::uint16_t>(wire.Owned.Counts[i]));
    for (const std::uint32_t key : wire.Shared.Keys)
        if (const GameplayTagId tag = LocalTag(context, key); tag.IsValid())
            (void)tags.Grant(tag);
}
