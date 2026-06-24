#include <framework/attributes/AttributeSetSerializer.h>

#include <framework/attributes/AttributeRegistry.h>
#include <framework/attributes/AttributeSerialization.h>
#include <framework/attributes/AttributeSet.h>

#include <core/serialization/FourCC.h>
#include <world/serialization/IComponentSerializer.h>

#include <memory>

// Declared in <world/serialization/SceneSerializer.h>; forward-declared here to
// keep framework code clear of the render/audio-pulling scene-codec headers (D-J).
void RegisterComponentSerializer(std::unique_ptr<IComponentSerializer> serializer);

namespace
{
    class AttributeSetSerializer final : public IComponentSerializer
    {
    public:
        std::string_view JsonKey() const override { return "Attributes"; }
        std::uint32_t BinaryChunkId() const override { return MakeFourCC('A', 'T', 'T', 'R'); }

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
