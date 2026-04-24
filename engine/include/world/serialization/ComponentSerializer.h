#pragma once

#include <core/metadata/SchemaVisit.h>
#include <core/metadata/TypeSchema.h>
#include <world/serialization/ComponentStorageTraits.h>
#include <world/serialization/IComponentSerializer.h>
#include <world/serialization/SceneFieldCodec.h>

#include <string_view>
#include <tuple>
#include <type_traits>

namespace SceneComponentSerialization
{
    template<typename Component>
    bool SaveFields(IWriteArchive& archive,
                    const Component& component,
                    SceneSerializationContext& context)
    {
        archive.BeginObject(std::string_view{});

        bool ok = true;
        auto fields = TypeSchema<Component>::Fields();
        std::apply([&](auto&... field)
        {
            ((ok = SceneFieldCodec<std::remove_cvref_t<decltype(component.*field.Ptr)>>::Save(
                archive, field.Name, component.*field.Ptr, context) && ok), ...);
        }, fields);

        archive.End();
        return ok && archive.Ok();
    }

    template<typename Component>
    bool LoadFields(IReadArchive& archive,
                    Component& component,
                    SceneSerializationContext& context)
    {
        archive.BeginObject(std::string_view{});

        bool ok = true;
        auto fields = TypeSchema<Component>::Fields();
        std::apply([&](auto&... field)
        {
            (([&]
            {
                if (!archive.HasField(field.Name))
                {
                    if (field.DefaultValue)
                        component.*field.Ptr = *field.DefaultValue;
                    else if (!field.IsOptional)
                        archive.MarkMissingField(field.Name);
                    ok = archive.Ok() && ok;
                    return;
                }

                using FieldType = std::remove_cvref_t<decltype(component.*field.Ptr)>;
                ok = SceneFieldCodec<FieldType>::Load(
                    archive, field.Name, component.*field.Ptr, context) && ok;
            }()), ...);
        }, fields);

        archive.End();
        return ok && archive.Ok();
    }
}

//=============================================================================
// ComponentSerializer
//
// Generic IComponentSerializer for any Component with a TypeSchema. Delegates
// storage access to ComponentStorageTraits<Component>.
//=============================================================================
template <typename Component>
    requires HasTypeSchema<Component>
class ComponentSerializer final : public IComponentSerializer
{
    using Traits = ComponentStorageTraits<Component>;

public:
    std::string_view JsonKey() const override { return TypeSchema<Component>::Name; }
    std::uint32_t BinaryChunkId() const override { return Traits::BinaryChunkId; }

    bool HasComponent(EntityId entity, const Registry& registry) const override
    {
        return registry.Components.IsRegistered<Component>()
            && registry.Components.HasComponent<Component>(entity);
    }

    bool Save(IWriteArchive& archive,
              EntityId entity,
              const Registry& registry,
              SceneSerializationContext& context) const override
    {
        const Component* component = registry.Components.IsRegistered<Component>()
            ? registry.Components.TryGet<Component>(entity)
            : nullptr;
        if (!component)
            return true;

        return SceneComponentSerialization::SaveFields(archive, *component, context);
    }

    bool Load(IReadArchive& archive,
              EntityId entity,
              Registry& registry,
              SceneSerializationContext& context) override
    {
        Component component{};
        if (!SceneComponentSerialization::LoadFields(archive, component, context))
            return false;

        return Traits::Add(registry, entity, component);
    }

    bool Remove(EntityId entity, Registry& registry) const override
    {
        if (!registry.Components.IsRegistered<Component>()
            || !registry.Components.HasComponent<Component>(entity))
        {
            return true;
        }
        registry.Components.RemoveComponent<Component>(entity);
        return true;
    }
};
