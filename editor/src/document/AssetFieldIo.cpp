#include "AssetFieldIo.h"

#include <core/assets/AssetRegistry.h>
#include <core/assets/AssetSystem.h>
#include <render/MaterialSetCache.h>
#include <render/static_mesh/StaticMeshHandle.h>

#include <cassert>
#include <cstring>
#include <span>
#include <utility>
#include <vector>

namespace
{
    // The field is type-erased (void* with the offset already applied); handles
    // are trivially copyable, so we move them in and out by bytes.
    template <typename Handle>
    Handle ReadHandle(const void* field)
    {
        Handle handle{};
        std::memcpy(&handle, field, sizeof(handle));
        return handle;
    }

    template <typename Handle>
    void WriteHandle(void* field, Handle handle)
    {
        std::memcpy(field, &handle, sizeof(handle));
    }

    // A path back to a reference: the id looked up through the catalog (invalid
    // when the asset has no stamped id, which ResolveRefPath then treats as
    // path-only). An empty path stays an empty (unset) ref.
    AssetFieldRef RefFromPath(AssetSystem& assets, std::string path, AssetType type)
    {
        AssetFieldRef ref;
        if (!path.empty())
            if (const AssetRecord* record = assets.Resolve(path, type); record != nullptr)
                ref.Id = record->Id;
        ref.Path = std::move(path);
        return ref;
    }

    std::string ResolvePath(AssetSystem& assets, const AssetFieldRef& ref, AssetType type)
    {
        return ref.Path.empty() ? std::string{}
                                : std::string(assets.ResolveRefPath(ref.Id, ref.Path, type));
    }
}

AssetFieldValue ReadAssetField(AssetSystem& assets, AssetType type,
                               AssetArity arity, const void* field)
{
    AssetFieldValue value;

    if (type == AssetType::StaticMesh && arity == AssetArity::Single)
    {
        std::string path(assets.GetPathForStaticMesh(ReadHandle<StaticMeshHandle>(field)));
        if (!path.empty())
            value.Refs.push_back(RefFromPath(assets, std::move(path), type));
        return value;
    }

    if (type == AssetType::Material && arity == AssetArity::List)
    {
        const MaterialSetHandle set = ReadHandle<MaterialSetHandle>(field);
        if (const std::vector<MaterialHandle>* members = assets.GetMaterialSet(set))
            for (const MaterialHandle material : *members)
                value.Refs.push_back(
                    RefFromPath(assets, std::string(assets.GetPathForMaterial(material)), type));
        return value;
    }

    assert(false && "ReadAssetField: unsupported (asset type, arity) shape");
    return value;
}

void ApplyAssetField(AssetSystem& assets, AssetType type, AssetArity arity,
                     void* field, const AssetFieldValue& value)
{
    if (type == AssetType::StaticMesh && arity == AssetArity::Single)
    {
        const std::string path = value.Refs.empty()
            ? std::string{}
            : ResolvePath(assets, value.Refs.front(), type);

        const StaticMeshHandle old = ReadHandle<StaticMeshHandle>(field);
        const StaticMeshHandle next = path.empty() ? StaticMeshHandle{}
                                                   : assets.LoadStaticMesh(path);
        WriteHandle(field, next);            // acquire-then-write
        assets.ReleaseStaticMesh(old);       // release the replaced handle last
        return;
    }

    if (type == AssetType::Material && arity == AssetArity::List)
    {
        // Build the new set in slot order. An unset slot keeps its position with
        // an invalid handle (slots are positional: index binds to a mesh section).
        // Loading each member up front retains it, so the materials are held
        // before the old set is released below.
        std::vector<MaterialHandle> materials;
        materials.reserve(value.Refs.size());
        for (const AssetFieldRef& ref : value.Refs)
        {
            const std::string path = ResolvePath(assets, ref, type);
            materials.push_back(path.empty() ? MaterialHandle{} : assets.LoadMaterial(path));
        }

        // Acquire the whole new set (it retains its own member refs) before
        // releasing the old set, so a material shared between an edited and an
        // unedited slot never reaches zero in between.
        const MaterialSetHandle next = assets.AcquireMaterialSet(materials);
        for (const MaterialHandle material : materials)
            if (material.IsValid())
                assets.ReleaseMaterial(material); // the set holds its own reference

        const MaterialSetHandle old = ReadHandle<MaterialSetHandle>(field);
        WriteHandle(field, next);
        assets.ReleaseMaterialSet(old);
        return;
    }

    assert(false && "ApplyAssetField: unsupported (asset type, arity) shape");
}
