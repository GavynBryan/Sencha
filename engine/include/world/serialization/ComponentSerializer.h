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
        const auto* store = registry.Components.TryGet<typename Traits::Store>();
        return store && store->TryGet(entity);
    }

    bool Save(IWriteArchive& archive,
              EntityId entity,
              const Registry& registry,
              SceneSerializationContext& context) const override
    {
        const auto* store = registry.Components.TryGet<typename Traits::Store>();
        const Component* component = store ? store->TryGet(entity) : nullptr;
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
        auto* store = registry.Components.TryGet<typename Traits::Store>();
        return !store || !store->TryGet(entity) || store->Remove(entity);
    }
};

//=============================================================================
// ComponentSerializer<TransformComponent<Transform3f>>
//
// Persists only the Local transform. World is excluded because it is
// recomputed by the propagation system after load.
//=============================================================================
template <>
class ComponentSerializer<TransformComponent<Transform3f>> final : public IComponentSerializer
{
    using Component = TransformComponent<Transform3f>;
    using Traits = ComponentStorageTraits<Component>;

public:
    std::string_view JsonKey() const override { return TypeSchema<Component>::Name; }
    std::uint32_t BinaryChunkId() const override { return Traits::BinaryChunkId; }

    bool HasComponent(EntityId entity, const Registry& registry) const override
    {
        const auto* store = registry.Components.TryGet<typename Traits::Store>();
        return store && store->TryGet(entity);
    }

    bool Save(IWriteArchive& archive,
              EntityId entity,
              const Registry& registry,
              SceneSerializationContext&) const override
    {
        const auto* store = registry.Components.TryGet<typename Traits::Store>();
        const Component* component = store ? store->TryGet(entity) : nullptr;
        if (!component)
            return true;

        WriteArchiveValue(archive, {}, component->Local);
        return archive.Ok();
    }

    bool Load(IReadArchive& archive,
              EntityId entity,
              Registry& registry,
              SceneSerializationContext&) override
    {
        Component component{};
        ReadArchiveValue(archive, {}, component.Local);
        // World is not persisted; propagation recalculates it on first update.
        component.World = Transform3f::Identity();
        if (!archive.Ok())
            return false;

        return Traits::Add(registry, entity, component);
    }

    bool Remove(EntityId entity, Registry& registry) const override
    {
        auto* store = registry.Components.TryGet<typename Traits::Store>();
        return !store || !store->TryGet(entity) || store->Remove(entity);
    }
};
