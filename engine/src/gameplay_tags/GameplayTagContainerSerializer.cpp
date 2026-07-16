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

// Declared in <world/serialization/SceneSerializer.h>. Forward-declared here so
// framework code does not include that header, which pulls ComponentSerializer ->
// SceneFieldCodec -> render/audio. Keeps the framework free of engine render/scene
// includes.
void RegisterComponentSerializer(std::unique_ptr<IComponentSerializer> serializer);

namespace
{
    class GameplayTagContainerSerializer final : public IComponentSerializer
    {
    public:
        ComponentTypeId TypeId() const override { return ResolveComponentTypeId<GameplayTagContainer>(); }
        std::string_view JsonKey() const override { return "GameplayTags"; }
        std::uint32_t BinaryChunkId() const override { return MakeFourCC('G', 'T', 'A', 'G'); }

        // No flat scalar leaves to expose: tags are a dynamic id/stack array, not
        // a fixed set of editable fields, and the framework does not carry a
        // TypeSchema. The inspector simply shows no leaf fields for this component.
        std::span<const RuntimeField> RuntimeFields() const override { return {}; }

        std::vector<std::byte> DefaultBytes() const override
        {
            GameplayTagContainer value{};
            std::vector<std::byte> bytes(sizeof(GameplayTagContainer));
            std::memcpy(bytes.data(), &value, sizeof(GameplayTagContainer));
            return bytes;
        }

        void RegisterStorage(Registry& registry) const override
        {
            if (!registry.Entities.IsRegistered<GameplayTagContainer>())
                registry.Entities.RegisterComponent<GameplayTagContainer>();
        }

        bool HasComponent(EntityId entity, const Registry& registry) const override
        {
            return registry.Entities.IsRegistered<GameplayTagContainer>()
                && registry.Entities.HasComponent<GameplayTagContainer>(entity);
        }

        bool Save(IWriteArchive& archive,
                  EntityId entity,
                  const Registry& registry,
                  SceneSerializationContext& context) const override
        {
            if (!registry.Entities.IsRegistered<GameplayTagContainer>())
                return true;
            const GameplayTagContainer* tags = registry.Entities.TryGet<GameplayTagContainer>(entity);
            if (!tags)
                return true; // entity has no tag component: nothing to write

            const GameplayTagRegistry* reg = context.GameplayTags;
            if (!reg)
                return false; // cannot persist names without the tag registry

            return WriteGameplayTags(archive, *tags, *reg);
        }

        bool Load(IReadArchive& archive,
                  EntityId entity,
                  Registry& registry,
                  SceneSerializationContext& context) override
        {
            GameplayTagRegistry* reg = context.GameplayTags;
            if (!reg)
                return false;

            GameplayTagContainer tags{};
            if (!ReadGameplayTags(archive, tags, *reg))
                return false;

            // Storage is registered up front by RegisterStorage (before entities
            // exist), so just attach; registering here would violate the
            // register-before-create rule.
            registry.Entities.AddComponent<GameplayTagContainer>(entity, tags);
            return true;
        }

        bool Remove(EntityId entity, Registry& registry) const override
        {
            if (registry.Entities.IsRegistered<GameplayTagContainer>()
                && registry.Entities.HasComponent<GameplayTagContainer>(entity))
            {
                registry.Entities.RemoveComponent<GameplayTagContainer>(entity);
            }
            return true;
        }
    };
}

void RegisterGameplayTagSerializer()
{
    RegisterComponentSerializer(std::make_unique<GameplayTagContainerSerializer>());
}
