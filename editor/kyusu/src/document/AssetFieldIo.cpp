#include "AssetFieldIo.h"

#include <core/assets/AssetLease.h>
#include <core/assets/AssetRegistry.h>
#include <assets/runtime/AssetSystem.h>
#include <render/MaterialSetCache.h>

#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace
{
    // The field is type-erased (void* with the offset already applied). Every
    // asset handle is an 8-byte token, and which store the token belongs to is
    // what the field's declared kind answers -- so the bytes move without this
    // code ever naming a handle type.
    std::uint64_t ReadToken(const void* field)
    {
        std::uint64_t token = 0;
        std::memcpy(&token, field, sizeof(token));
        return token;
    }

    void WriteToken(void* field, std::uint64_t token)
    {
        std::memcpy(field, &token, sizeof(token));
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

    bool IsCompositeSet(AssetType type, AssetArity arity)
    {
        return arity == AssetArity::List && type == AssetType::Material;
    }
}

AssetFieldValue ReadAssetField(AssetSystem& assets, AssetType type,
                               AssetArity arity, const void* field)
{
    AssetFieldValue value;

    if (IsCompositeSet(type, arity))
    {
        const MaterialSetHandle set = MaterialSetHandle::FromToken(ReadToken(field));
        if (const std::vector<MaterialHandle>* members = assets.GetMaterialSet(set))
            for (const MaterialHandle material : *members)
                value.Refs.push_back(
                    RefFromPath(assets, std::string(assets.GetPathForMaterial(material)), type));
        return value;
    }

    assert(arity == AssetArity::Single && "only a material list has a composite field form");

    std::string path(assets.GetPathForLease(type, ReadToken(field)));
    if (!path.empty())
        value.Refs.push_back(RefFromPath(assets, std::move(path), type));
    return value;
}

void ApplyAssetField(AssetSystem& assets, AssetType type, AssetArity arity,
                     void* field, const AssetFieldValue& value)
{
    if (IsCompositeSet(type, arity))
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

        const MaterialSetHandle old = MaterialSetHandle::FromToken(ReadToken(field));
        WriteToken(field, next.ToToken());
        assets.ReleaseMaterialSet(old);
        return;
    }

    assert(arity == AssetArity::Single && "only a material list has a composite field form");

    const std::string path = value.Refs.empty()
        ? std::string{}
        : ResolvePath(assets, value.Refs.front(), type);

    const std::uint64_t old = ReadToken(field);
    // The load's own reference becomes the field's: the component's lifecycle
    // hooks release what the field holds, so the lease must not also drop it on
    // the way out of this scope.
    AssetLease next = path.empty() ? AssetLease{} : assets.LoadLease(path, type);
    WriteToken(field, next.Relinquish()); // acquire-then-write
    assets.ReleaseLease(type, old);       // release the replaced handle last
}
