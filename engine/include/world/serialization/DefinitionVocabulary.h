#pragma once

#include <ecs/World.h>
#include <world/serialization/NamedIdVocabulary.h>

#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

// The vocabulary of a NamedDefinitionRegistry resource: a read reserves the name it is given.
template <typename Registry>
[[nodiscard]] NamedIdVocabulary MakeDefinitionVocabulary(std::string_view label, std::string_view subtype)
{
    using Id = decltype(std::declval<const Registry&>().Find(std::string_view{}));
    return NamedIdVocabulary{
        .Label = label,
        .Kind = NamedIdKind::DataDefinition,
        .DataSubtype = subtype,
        .NameOf = [](const World& world, std::uint32_t id) -> std::string_view {
            const Registry* registry = world.TryGetResource<Registry>();
            return registry != nullptr ? registry->GetName(Id{ id }) : std::string_view{};
        },
        .Find = [](const World& world, std::string_view name) -> std::uint32_t {
            const Registry* registry = world.TryGetResource<Registry>();
            return registry != nullptr ? registry->Find(name).Value : 0u;
        },
        .Resolve = [](World& world, std::string_view name) -> std::uint32_t {
            Registry* registry = world.TryGetResource<Registry>();
            return registry != nullptr ? registry->Reserve(name).Value : 0u;
        },
        .Names = [](const World& world, std::vector<std::string_view>& out) {
            if (const Registry* registry = world.TryGetResource<Registry>())
                registry->ForEachName([&](Id, std::string_view name) { out.push_back(name); });
        },
        .WireKey = [](const World& world, std::uint32_t id) -> std::uint32_t {
            const Registry* registry = world.TryGetResource<Registry>();
            return registry != nullptr ? registry->WireKey(Id{ id }) : 0u;
        },
        .FromWireKey = [](const World& world, std::uint32_t key) -> std::uint32_t {
            const Registry* registry = world.TryGetResource<Registry>();
            return registry != nullptr && key != 0 ? registry->FindByWireKey(key).Value : 0u;
        },
    };
}
