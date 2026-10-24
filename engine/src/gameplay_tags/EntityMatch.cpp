#include <gameplay_tags/EntityMatch.h>

#include <ecs/World.h>
#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagQuery.h>

#include <algorithm>

bool EntityMatches(const World& world,
                   EntityId entity,
                   std::span<const ComponentId> components,
                   const GameplayTagQuery* tags,
                   const GameplayTagRegistry* registry)
{
    if (!world.IsAlive(entity))
        return false;
    if (std::ranges::any_of(components, [&](ComponentId component) { return !world.HasComponent(entity, component); }))
        return false;
    if (tags == nullptr)
        return true;
    static const GameplayTagContainer kNoTags{};
    const GameplayTagContainer* held =
        world.IsRegistered<GameplayTagContainer>() ? world.TryGet<GameplayTagContainer>(entity) : nullptr;
    return registry != nullptr && tags->Matches(held != nullptr ? *held : kNoTags, *registry);
}
