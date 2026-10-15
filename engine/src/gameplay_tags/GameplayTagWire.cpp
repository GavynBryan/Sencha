#include <gameplay_tags/GameplayTagWire.h>

#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <net/ReplicationWireContext.h>

namespace
{
const GameplayTagRegistry* TagsOf(const ReplicationWireContext& context)
{
    return context.Entities != nullptr ? context.Entities->TryGetResource<GameplayTagRegistry>() : nullptr;
}
}

std::uint32_t WireTag(const ReplicationWireContext& context, GameplayTagId tag)
{
    const GameplayTagRegistry* tags = TagsOf(context);
    return tags != nullptr && tag.IsValid() ? tags->WireKey(tag) : 0u;
}

GameplayTagId LocalTag(const ReplicationWireContext& context, std::uint32_t key)
{
    const GameplayTagRegistry* tags = TagsOf(context);
    return tags != nullptr && key != 0 ? tags->FindByWireKey(key) : GameplayTagId{};
}
