#include <gameplay_tags/GameplayTagContainerSerializer.h>

#include <gameplay_tags/GameplayTagContainer.h>
#include <gameplay_tags/GameplayTagRegistry.h>
#include <gameplay_tags/GameplayTagSerialization.h>

#include <core/serialization/FourCC.h>
#include <ecs/ComponentTypeId.h>
#include <world/serialization/IComponentSerializer.h>

#include <cstddef>
#include <cstring>
#include <memory>
#include <span>
#include <vector>
#include <world/serialization/ComponentSerializerRegistry.h>

namespace
{
class GameplayTagContainerSerializer final : public IComponentSerializer
{
public:
    ComponentTypeId TypeId() const override
    {
        return ResolveComponentTypeId<GameplayTagContainer>();
    }

    std::string_view JsonKey() const override { return "GameplayTags"; }
    std::uint32_t BinaryChunkId() const override
    {
        return MakeFourCC('G', 'T', 'A', 'G');
    }

    std::span<const RuntimeField> RuntimeFields() const override
    {
        return {};
    }

    std::vector<std::byte> DefaultBytes() const override
    {
        GameplayTagContainer value{};
        std::vector<std::byte> bytes(sizeof(GameplayTagContainer));
        std::memcpy(bytes.data(), &value, sizeof(GameplayTagContainer));
        return bytes;
    }

    void RegisterStorage(Registry& registry) const override
    {
        if (!registry.Components.IsRegistered<GameplayTagContainer>())
            registry.Components.RegisterComponent<GameplayTagContainer>();
    }

    bool HasComponent(
        EntityId entity,
        const Registry& registry) const override
    {
        return registry.Components.IsRegistered<GameplayTagContainer>()
            && registry.Components.HasComponent<GameplayTagContainer>(entity);
    }

    bool Save(
        IWriteArchive& archive,
        EntityId entity,
        const Registry& registry,
        SceneSerializationContext&) const override
    {
        if (!registry.Components.IsRegistered<GameplayTagContainer>())
            return true;

        const GameplayTagContainer* tags =
            registry.Components.TryGet<GameplayTagContainer>(entity);
        if (tags == nullptr)
            return true;

        const GameplayTagRegistry* tagRegistry =
            registry.Components.TryGetResource<GameplayTagRegistry>();
        return tagRegistry != nullptr
            && WriteGameplayTags(archive, *tags, *tagRegistry);
    }

    bool Load(
        IReadArchive& archive,
        EntityId entity,
        Registry& registry,
        SceneSerializationContext& context) override
    {
        return LoadIntoWorld(
            archive,
            entity,
            registry.Components,
            context);
    }

    bool LoadIntoWorld(
        IReadArchive& archive,
        EntityId entity,
        World& world,
        SceneSerializationContext&) override
    {
        GameplayTagRegistry* tagRegistry =
            world.TryGetResource<GameplayTagRegistry>();
        if (tagRegistry == nullptr)
            return false;

        // A batch importer creates the entity at its final archetype signature,
        // so the column may already be present. Presence is then expected rather
        // than a duplicate, and the value is written in place.
        const bool preallocated = world.HasComponent<GameplayTagContainer>(entity);

        GameplayTagContainer tags{};
        if (!ReadGameplayTags(archive, tags, *tagRegistry))
            return false;

        if (preallocated)
            return world.InitializeComponent<GameplayTagContainer>(entity, tags);

        world.AddComponent<GameplayTagContainer>(entity, tags);
        return true;
    }

    bool Remove(EntityId entity, Registry& registry) const override
    {
        if (registry.Components.IsRegistered<GameplayTagContainer>()
            && registry.Components.HasComponent<GameplayTagContainer>(entity))
        {
            registry.Components.RemoveComponent<GameplayTagContainer>(entity);
        }
        return true;
    }
};
} // namespace

void RegisterGameplayTagSerializer(ComponentSerializerRegistry& serializers)
{
    (void)serializers.Register(
        std::make_unique<GameplayTagContainerSerializer>());
}
