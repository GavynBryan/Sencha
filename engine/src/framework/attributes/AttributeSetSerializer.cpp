#include <framework/attributes/AttributeSetSerializer.h>

#include <framework/attributes/AttributeRegistry.h>
#include <framework/attributes/AttributeSerialization.h>
#include <framework/attributes/AttributeSet.h>

#include <core/serialization/FourCC.h>
#include <ecs/ComponentTypeId.h>
#include <world/serialization/IComponentSerializer.h>

#include <cstddef>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

// Declared in <world/serialization/SceneSerializer.h>; forward-declared here to
// keep framework code clear of the render/audio-pulling scene-codec headers.
void RegisterComponentSerializer(std::unique_ptr<IComponentSerializer> serializer);

namespace
{
    class AttributeSetSerializer final : public IComponentSerializer
    {
    public:
        ComponentTypeId TypeId() const override { return ResolveComponentTypeId<AttributeSet>(); }
        std::string_view JsonKey() const override { return "Attributes"; }
        std::uint32_t BinaryChunkId() const override { return MakeFourCC('A', 'T', 'T', 'R'); }

        // No flat scalar leaves to expose: attributes are a dynamic id/value array,
        // not a fixed field set, and the framework does not carry a TypeSchema.
        std::span<const RuntimeField> RuntimeFields() const override { return {}; }

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

        bool HasComponent(EntityId entity, const Registry& registry) const override
        {
            return registry.Components.IsRegistered<AttributeSet>()
                && registry.Components.HasComponent<AttributeSet>(entity);
        }

        bool Save(IWriteArchive& archive,
                  EntityId entity,
                  const Registry& registry,
                  SceneSerializationContext&) const override
        {
            if (!registry.Components.IsRegistered<AttributeSet>())
                return true;
            const AttributeSet* set = registry.Components.TryGet<AttributeSet>(entity);
            if (!set)
                return true;

            const AttributeRegistry* reg = registry.Components.TryGetResource<AttributeRegistry>();
            if (!reg)
                return false;

            return WriteAttributes(archive, *set, *reg);
        }

        bool Load(IReadArchive& archive,
                  EntityId entity,
                  Registry& registry,
                  SceneSerializationContext&) override
        {
            AttributeRegistry* reg = registry.Components.TryGetResource<AttributeRegistry>();
            if (!reg)
                return false;

            AttributeSet set{};
            if (!ReadAttributes(archive, set, *reg))
                return false;

            registry.Components.AddComponent<AttributeSet>(entity, set);
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
}

void RegisterAttributeSerializer()
{
    RegisterComponentSerializer(std::make_unique<AttributeSetSerializer>());
}
