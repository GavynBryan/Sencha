#pragma once

#include <core/assets/AssetStoreTable.h>
#include <core/metadata/RuntimeSchema.h>
#include <ecs/EntityId.h>

#include <span>
#include <vector>

class World;

//=============================================================================
// SchemaAssetOwnership
//
// Lifecycle hooks for a component whose schema names asset fields. A field
// tagged AsAsset is an owning reference: retained through the World's
// AssetStoreTable when the component is added, released when it is removed.
// A component's traits inherit them:
//
//   template <>
//   struct ComponentTraits<StaticMeshComponent>
//       : SchemaAssetOwnership<StaticMeshComponent> {};
//
// A component with more to do on removal declares its own OnRemove and calls
// this one last, so the asset outlives whatever still uses it.
//
// Fields are retained in schema order and released in reverse. The table is
// the World's AssetStoreTable resource, which the host that owns the caches
// sets and, before they go away, replaces with an empty one. A World without
// one, or a table with no store for a field's kind, holds nothing for that
// field.
//=============================================================================

// The asset-bearing leaves of T's flattened authoring schema, nested schemas
// included.
template <typename T>
    requires HasTypeSchema<T>
std::span<const RuntimeField> AssetFieldsOf()
{
    static const std::vector<RuntimeField> fields = []
    {
        std::vector<RuntimeField> out;
        for (const RuntimeField& field : RuntimeFieldsOf<T>())
        {
            if (field.Asset != AssetType::Unknown)
                out.push_back(field);
        }
        return out;
    }();
    return fields;
}

void RetainAssetFields(const void* component,
                       std::span<const RuntimeField> fields,
                       World& world);
void ReleaseAssetFields(const void* component,
                        std::span<const RuntimeField> fields,
                        World& world);

template <typename T>
struct SchemaAssetOwnership
{
    static void OnAdd(T& component, World& world, EntityId)
    {
        RetainAssetFields(&component, AssetFieldsOf<T>(), world);
    }

    static void OnRemove(const T& component, World& world, EntityId)
    {
        ReleaseAssetFields(&component, AssetFieldsOf<T>(), world);
    }
};

