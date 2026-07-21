#include <attributes/AttributeSetSerializer.h>

#include <attributes/AttributeRegistry.h>
#include <attributes/AttributeSerialization.h>
#include <attributes/AttributeSet.h>

#include <core/serialization/FourCC.h>
#include <ecs/ComponentTypeId.h>
#include <world/serialization/IComponentSerializer.h>

#include <cstddef>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

void RegisterComponentSerializer(std::unique_ptr<IComponentSerializer> serializer);

namespace
{
class AttributeSetSerializer final : public IComponentSerializer
{
public:
    ComponentTypeId TypeId() const override
    {
        return ResolveComponentTypeId<AttributeSet>();
    }

    std::string_view JsonKey() const override { return "Attributes"; }
    std::uint32_t BinaryChunkId() const override
    {
        return MakeFourCC('A', 'T', 'T', 'R');
    }

    std::span<const RuntimeField> RuntimeFields() const override
    {
        return {};
    }

    std::vector<std::byte> DefaultBytes() const override
    {
        AttributeSet value{};
        std::vector<std::byte> bytes(sizeof(AttributeSet));
        std::memcpy(bytes.data(), &value, sizeof(AttributeSet));
        return bytes;
    }

    void RegisterStorage(Registry& registry) const override
    {
        if (!registry.Components.IsRegistered<AttributeSet>())
            registry.Components.RegisterComponent<AttributeSet>();
    }

    bool HasComponent(
        EntityId entity,
        const Registry& registry) const override
    {
        return registry.Components.IsRegistered<AttributeSet>()
            && registry.Components.HasComponent<AttributeSet>(entity);
    }

    bool Save(
        IWriteArchive& archive,
        EntityId entity,
        const Registry& registry,
        SceneSerializationContext&) const override
    {
        if (!registry.Components.IsRegistered<AttributeSet>())
            return true;

        const AttributeSet* set =
            registry.Components.TryGet<AttributeSet>(entity);
        if (set == nullptr)
            return true;

        const AttributeRegistry* attributes =
            registry.Components.TryGetResource<AttributeRegistry>();
        return attributes != nullptr
            && WriteAttributes(archive, *set, *attributes);
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
        AttributeRegistry* attributes =
            world.TryGetResource<AttributeRegistry>();
        if (attributes == nullptr
            || world.HasComponent<AttributeSet>(entity))
        {
            return false;
        }

        AttributeSet set{};
        if (!ReadAttributes(archive, set, *attributes))
            return false;

        world.AddComponent<AttributeSet>(entity, set);
        return true;
    }

    bool Remove(EntityId entity, Registry& registry) const override
    {
        if (registry.Components.IsRegistered<AttributeSet>()
            && registry.Components.HasComponent<AttributeSet>(entity))
        {
            registry.Components.RemoveComponent<AttributeSet>(entity);
        }
        return true;
    }
};
} // namespace

void RegisterAttributeSerializer()
{
    RegisterComponentSerializer(
        std::make_unique<AttributeSetSerializer>());
}
