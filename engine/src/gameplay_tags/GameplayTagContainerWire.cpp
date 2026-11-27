#include <gameplay_tags/GameplayTagContainerWire.h>

#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <gameplay_tags/GameplayTagWire.h>

#include <algorithm>
#include <array>

namespace
{
const GameplayTagRegistry* TagsOf(const ReplicationWireContext& context)
{
    return context.Entities != nullptr ? context.Entities->TryGetResource<GameplayTagRegistry>() : nullptr;
}
}

void ReplicationCodec<GameplayTagContainer>::ToWire(const ReplicationWireContext& context,
                                                    const GameplayTagContainer& tags, ReplicationImage& wire)
{
    const GameplayTagRegistry* registry = TagsOf(context);
    std::array<GameplayTagOwnedWire, GameplayTagContainer::Capacity> owned{};
    std::array<GameplayTagSharedWire, GameplayTagContainer::Capacity> shared{};
    std::size_t ownedCount = 0;
    std::size_t sharedCount = 0;
    for (std::uint8_t i = 0; i < tags.Count; ++i)
    {
        const std::uint32_t key = WireTag(context, tags.Tags[i]);
        if (key == 0)
            continue;
        if (registry != nullptr && registry->IsReplicatedToAll(tags.Tags[i]))
            shared[sharedCount++].Key = key;
        else
            owned[ownedCount++] = GameplayTagOwnedWire{ .Key = key, .Count = tags.Counts[i] };
    }
    std::sort(owned.begin(), owned.begin() + ownedCount,
              [](const GameplayTagOwnedWire& a, const GameplayTagOwnedWire& b) { return a.Key < b.Key; });
    std::sort(shared.begin(), shared.begin() + sharedCount,
              [](const GameplayTagSharedWire& a, const GameplayTagSharedWire& b) { return a.Key < b.Key; });
    const std::span<GameplayTagSharedWire> sentShared = wire.Resize<GameplayTagSharedWire>(1, sharedCount);
    std::copy_n(shared.begin(), sentShared.size(), sentShared.begin());
    const std::span<GameplayTagOwnedWire> sentOwned = wire.Resize<GameplayTagOwnedWire>(0, ownedCount);
    std::copy_n(owned.begin(), sentOwned.size(), sentOwned.begin());
}

void ReplicationCodec<GameplayTagContainer>::FromWire(const ReplicationWireContext& context,
                                                      const ReplicationImage& wire, GameplayTagContainer& tags)
{
    tags.Clear();
    for (const GameplayTagOwnedWire& sent : wire.List<GameplayTagOwnedWire>(0))
        if (const GameplayTagId tag = LocalTag(context, sent.Key); tag.IsValid() && sent.Count > 0)
            (void)tags.Grant(tag, static_cast<std::uint16_t>(sent.Count));
    for (const GameplayTagSharedWire& sent : wire.List<GameplayTagSharedWire>(1))
        if (const GameplayTagId tag = LocalTag(context, sent.Key); tag.IsValid())
            (void)tags.Grant(tag);
}
