#pragma once

#include <core/handle/Handle.h>
#include <core/metadata/ComponentRemovable.h>
#include <core/metadata/SchemaVisit.h>
#include <core/metadata/TypeSchema.h>
#include <world/serialization/ComponentStorageTraits.h>
#include <world/serialization/IComponentSerializer.h>
#include <world/serialization/SceneAssetFieldIo.h>
#include <world/serialization/SceneFieldCodec.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <vector>

namespace SceneComponentSerialization
{
    // A handle member is an asset reference, and its schema says which kind.
    // Nothing else can say it: the handle type is deliberately opaque and the
    // same Handle<Tag> shape backs every kind, so the tag on the field is the
    // only statement of what the reference means.
    template<typename FieldT>
    void AssertTagged([[maybe_unused]] const FieldT& field)
    {
        assert(field.Asset != AssetType::Unknown
               && "a handle field must declare its asset kind with .AsAsset(): "
                  "a handle has no persisted form of its own");
    }

    template<typename Component, typename FieldT>
    bool SaveField(IWriteArchive& archive,
                   const Component& component,
                   const FieldT& field,
                   SceneSerializationContext& context)
    {
        using FieldType = std::remove_cvref_t<decltype(component.*field.Ptr)>;
        if constexpr (IsHandleType<FieldType>)
        {
            AssertTagged(field);
            return SaveAssetField(archive, field.Name, (component.*field.Ptr).ToToken(),
                                  field.Asset, field.Arity, context);
        }
        else
        {
            return SceneFieldCodec<FieldType>::Save(
                archive, field.Name, component.*field.Ptr, context);
        }
    }

    template<typename Component, typename FieldT>
    bool LoadField(IReadArchive& archive,
                   Component& component,
                   const FieldT& field,
                   SceneSerializationContext& context)
    {
        using FieldType = std::remove_cvref_t<decltype(component.*field.Ptr)>;
        if constexpr (IsHandleType<FieldType>)
        {
            AssertTagged(field);
            std::uint64_t token = (component.*field.Ptr).ToToken();
            const bool ok = LoadAssetField(archive, field.Name, token,
                                           field.Asset, field.Arity, context);
            component.*field.Ptr = FieldType::FromToken(token);
            return ok;
        }
        else
        {
            return SceneFieldCodec<FieldType>::Load(
                archive, field.Name, component.*field.Ptr, context);
        }
    }

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
            ((ok = SaveField(archive, component, field, context) && ok), ...);
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

                ok = LoadField(archive, component, field, context) && ok;
            }()), ...);
        }, fields);

        archive.End();
        return ok && archive.Ok();
    }

    // Drops whatever LoadFields acquired. Called once the loaded value has
    // been handed to the entity that will own it -- the component's own
    // lifecycle hooks take its reference -- and on every path where the value
    // is discarded instead. Field types that acquire nothing declare no
    // Release and are skipped.
    template<typename Component>
    void ReleaseFields(Component& component, SceneSerializationContext& context)
    {
        auto fields = TypeSchema<Component>::Fields();
        std::apply([&](auto&... field)
        {
            (([&]
            {
                using FieldType = std::remove_cvref_t<decltype(component.*field.Ptr)>;
                if constexpr (IsHandleType<FieldType>)
                {
                    std::uint64_t token = (component.*field.Ptr).ToToken();
                    ReleaseAssetField(token, field.Asset, field.Arity, context);
                    component.*field.Ptr = FieldType::FromToken(token);
                }
                else if constexpr (requires (FieldType& value) {
                                       SceneFieldCodec<FieldType>::Release(value, context);
                                   })
                {
                    SceneFieldCodec<FieldType>::Release(component.*field.Ptr, context);
                }
            }()), ...);
        }, fields);
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
    ComponentTypeId TypeId() const override { return ResolveComponentTypeId<Component>(); }
    std::string_view JsonKey() const override { return TypeSchema<Component>::Name; }
    std::uint32_t BinaryChunkId() const override { return Traits::BinaryChunkId; }

    std::span<const RuntimeField> RuntimeFields() const override
    {
        return RuntimeFieldsOf<Component>();
    }

    std::optional<EditorVisual> GetEditorVisual() const override
    {
        return ComponentEditorVisual<Component>::Value;
    }

    bool IsRemovable() const override
    {
        return ComponentRemovable<Component>::Value;
    }

    std::vector<std::byte> DefaultBytes() const override
    {
        if constexpr (std::is_empty_v<Component>)
        {
            return {};
        }
        else
        {
            Component value{}; // C++ default member initializers
            std::vector<std::byte> bytes(sizeof(Component));
            std::memcpy(bytes.data(), &value, sizeof(Component));
            return bytes;
        }
    }

    void RegisterStorage(Registry& registry) const override
    {
        Traits::Register(registry);
    }

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
        if constexpr (std::is_empty_v<Component>)
        {
            // A tag's presence is its whole value: it has no column, so
            // TryGet answers null even when the signature bit is set.
            if (!registry.Components.IsRegistered<Component>()
                || !registry.Components.HasComponent<Component>(entity))
                return true;
            const Component tag{};
            return SceneComponentSerialization::SaveFields(archive, tag, context);
        }
        else
        {
            const Component* component =
                registry.Components.IsRegistered<Component>()
                    ? registry.Components.TryGet<Component>(entity)
                    : nullptr;
            if (!component)
                return true;

            return SceneComponentSerialization::SaveFields(archive, *component,
                                                           context);
        }
    }

    bool Load(IReadArchive& archive,
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

    bool LoadIntoWorld(IReadArchive& archive,
                       EntityId entity,
                       World& world,
                       SceneSerializationContext& context) override
    {
        Component component{};
        if (!SceneComponentSerialization::LoadFields(archive, component, context))
        {
            // A partial load still acquired whatever it got through.
            SceneComponentSerialization::ReleaseFields(component, context);
            return false;
        }

        // A batch importer creates the entity at its final archetype signature,
        // so the column is already there and Traits::Add would read the presence
        // as a duplicate. Write in place instead; OnAdd still fires exactly once.
        // Rows the editor's document path loads into are never pre-created, so
        // that path always takes the branch below.
        const bool added = world.HasComponent<Component>(entity)
            ? world.InitializeComponent<Component>(entity, component)
            : Traits::Add(world, entity, component);

        // Either the entity's copy now owns its own reference, taken by OnAdd,
        // or nothing was added and nothing owns one. The load's reference is
        // spent either way.
        SceneComponentSerialization::ReleaseFields(component, context);
        return added;
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
