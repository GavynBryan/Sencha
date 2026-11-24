#include <gameplay_tags/GameplayTagContainerSerializer.h>

#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <gameplay_tags/GameplayTagSerialization.h>

#include <core/logging/LoggingProvider.h>
#include <core/serialization/FourCC.h>
#include <ecs/ComponentTypeId.h>
#include <world/serialization/TypedComponentSerializer.h>

#include <cstddef>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace
{
class GameplayTagContainerSerializer final : public TypedComponentSerializer<GameplayTagContainer>
{
public:
    std::string_view JsonKey() const override { return "GameplayTags"; }
    const DataFieldSchema* CompositionSchema() const override
    {
        static const DataFieldSchema schema = MakeKeyedListRecordSchema("tags", "name");
        return &schema;
    }
    std::uint32_t BinaryChunkId() const override
    {
        return MakeFourCC('G', 'T', 'A', 'G');
    }

    std::span<const RuntimeField> RuntimeFields() const override
    {
        return {};
    }

    bool Save(
        IWriteArchive& archive,
        EntityId entity,
        const World& world,
        SceneSerializationContext&) const override
    {
        if (!world.IsRegistered<GameplayTagContainer>())
            return true;

        const GameplayTagContainer* tags =
            world.TryGet<GameplayTagContainer>(entity);
        if (tags == nullptr)
            return true;

        const GameplayTagRegistry* tagRegistry =
            world.TryGetResource<GameplayTagRegistry>();
        return tagRegistry != nullptr
            && WriteGameplayTags(archive, *tags, *tagRegistry);
    }

    bool SavedFormFromRowAlone() const override { return true; }

    bool SaveDefault(IWriteArchive& archive) const override
    {
        static const GameplayTagRegistry kNoTags;
        return WriteGameplayTags(archive, GameplayTagContainer{}, kNoTags);
    }

    bool LoadIntoWorld(
        IReadArchive& archive,
        EntityId entity,
        World& world,
        SceneSerializationContext& context) override
    {
        GameplayTagRegistry* tagRegistry =
            world.TryGetResource<GameplayTagRegistry>();
        if (tagRegistry == nullptr)
            return false;

        GameplayTagContainer tags{};
        std::vector<DroppedGameplayTag> dropped;
        if (!ReadGameplayTags(archive, tags, *tagRegistry, &dropped))
            return false;
        const Logger& log = context.Logging->GetLogger<SceneSerializationContext>();
        for (const DroppedGameplayTag& tag : dropped)
            if (tag.Reason == GameplayTagDrop::Projected)
                log.Warn("gameplay tag '{}' is projected by {} and cannot be authored; it was not loaded", tag.Name,
                         tagRegistry->ProjectedBy(tagRegistry->FindTag(tag.Name)));
            else
                log.Warn("gameplay tag '{}' was not loaded: an entity holds at most {} tags", tag.Name,
                         GameplayTagContainer::Capacity);

        return Place(world, entity, tags);
    }
};
} // namespace

std::unique_ptr<IComponentSerializer> MakeGameplayTagContainerSerializer()
{
    return std::make_unique<GameplayTagContainerSerializer>();
}
