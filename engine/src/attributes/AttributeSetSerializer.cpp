#include <attributes/AttributeSetSerializer.h>

#include <attributes/AttributeRegistry.h>
#include <attributes/AttributeSerialization.h>
#include <attributes/AttributeSet.h>

#include <core/serialization/FourCC.h>
#include <ecs/ComponentTypeId.h>
#include <world/serialization/TypedComponentSerializer.h>

#include <cstddef>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

namespace
{
class AttributeSetSerializer final : public TypedComponentSerializer<AttributeSet>
{
public:
    std::string_view JsonKey() const override { return "Attributes"; }
    const DataFieldSchema* CompositionSchema() const override
    {
        static const DataFieldSchema schema = MakeKeyedListRecordSchema("attributes", "name");
        return &schema;
    }
    std::uint32_t BinaryChunkId() const override
    {
        return MakeFourCC('A', 'T', 'T', 'R');
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
        if (!world.IsRegistered<AttributeSet>())
            return true;

        const AttributeSet* set =
            world.TryGet<AttributeSet>(entity);
        if (set == nullptr)
            return true;

        const AttributeRegistry* attributes =
            world.TryGetResource<AttributeRegistry>();
        return attributes != nullptr
            && WriteAttributes(archive, *set, *attributes);
    }

    bool LoadIntoWorld(
        IReadArchive& archive,
        EntityId entity,
        World& world,
        SceneSerializationContext&) override
    {
        AttributeRegistry* attributes =
            world.TryGetResource<AttributeRegistry>();
        if (attributes == nullptr)
            return false;

        AttributeSet set{};
        if (!ReadAttributes(archive, set, *attributes))
            return false;

        return Place(world, entity, set);
    }
};
} // namespace

std::unique_ptr<IComponentSerializer> MakeAttributeSetSerializer()
{
    return std::make_unique<AttributeSetSerializer>();
}
