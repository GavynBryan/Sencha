#include <gameplay_tags/GameplayTagId.h>

#include <ecs/World.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <world/serialization/NamedIdVocabulary.h>

namespace
{
const GameplayTagRegistry* TagsOf(const World& world)
{
    return world.TryGetResource<GameplayTagRegistry>();
}
}

const NamedIdVocabulary& NamedIdField<GameplayTagId>::Vocabulary()
{
    static const NamedIdVocabulary vocabulary{
        .Label = "gameplay tag",
        .Kind = Kind,
        .DataSubtype = {},
        .NameOf = [](const World& world, std::uint32_t id) -> std::string_view {
            const GameplayTagRegistry* tags = TagsOf(world);
            return tags != nullptr ? tags->GetName(GameplayTagId{ id }) : std::string_view{};
        },
        .Find = [](const World& world, std::string_view name) -> std::uint32_t {
            const GameplayTagRegistry* tags = TagsOf(world);
            return tags != nullptr && !name.empty() ? tags->FindTag(name).Value : 0u;
        },
        .Resolve = [](World& world, std::string_view name) -> std::uint32_t {
            const GameplayTagRegistry* tags = TagsOf(world);
            return tags != nullptr && !name.empty() ? tags->FindTag(name).Value : 0u;
        },
        .Names = [](const World& world, std::vector<std::string_view>& out) {
            if (const GameplayTagRegistry* tags = TagsOf(world))
                for (std::uint32_t id = 1; id <= tags->Size(); ++id)
                    out.push_back(tags->GetName(GameplayTagId{ id }));
        },
        .WireKey = [](const World& world, std::uint32_t id) -> std::uint32_t {
            const GameplayTagRegistry* tags = TagsOf(world);
            return tags != nullptr && id != 0 ? tags->WireKey(GameplayTagId{ id }) : 0u;
        },
        .FromWireKey = [](const World& world, std::uint32_t key) -> std::uint32_t {
            const GameplayTagRegistry* tags = TagsOf(world);
            return tags != nullptr && key != 0 ? tags->FindByWireKey(key).Value : 0u;
        },
    };
    return vocabulary;
}
