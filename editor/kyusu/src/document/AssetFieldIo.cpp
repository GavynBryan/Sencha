#include "AssetFieldIo.h"

#include <core/assets/AssetRegistry.h>
#include <core/assets/AssetSystem.h>
#include <render/MaterialSetCache.h>

#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
    // The field is type-erased (void* with the offset already applied). Every
    // asset handle is a trivially copyable 8-byte {Index, Generation}; moving it
    // as the canonical uint64 token (low=Index, high=Generation) is byte-
    // identical to the handle's memory on little-endian, so a concrete handle
    // reconstructs via Handle<Tag>::FromToken(ReadHandleToken(field)).
    uint64_t ReadHandleToken(const void* field)
    {
        uint64_t token = 0;
        std::memcpy(&token, field, sizeof(token));
        return token;
    }

    void WriteHandleToken(void* field, uint64_t token)
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

    // Single-arity: one handle addressed through AssetSystem's type-generic ops,
    // so the concrete handle type never appears here.
    AssetFieldValue ReadSingleHandle(AssetSystem& assets, AssetType type, const void* field)
    {
        AssetFieldValue value;
        std::string path(assets.GetAssetHandlePath(type, ReadHandleToken(field)));
        if (!path.empty())
            value.Refs.push_back(RefFromPath(assets, std::move(path), type));
        return value;
    }

    void ApplySingleHandle(AssetSystem& assets, AssetType type, void* field,
                           const AssetFieldValue& value)
    {
        const std::string path = value.Refs.empty()
            ? std::string{}
            : ResolvePath(assets, value.Refs.front(), type);

        const uint64_t old = ReadHandleToken(field);
        const uint64_t next = path.empty() ? 0 : assets.LoadAssetHandle(type, path);
        WriteHandleToken(field, next);          // acquire-then-write
        assets.ReleaseAssetHandle(type, old);   // release the replaced handle last
    }

    // List-arity (per-slot materials): a MaterialSet, its own shape. Slots are
    // positional (index binds to a mesh section); an unset slot keeps its place
    // with an invalid handle.
    AssetFieldValue ReadMaterialList(AssetSystem& assets, AssetType type, const void* field)
    {
        AssetFieldValue value;
        const MaterialSetHandle set = MaterialSetHandle::FromToken(ReadHandleToken(field));
        if (const std::vector<MaterialHandle>* members = assets.GetMaterialSet(set))
            for (const MaterialHandle material : *members)
                value.Refs.push_back(
                    RefFromPath(assets, std::string(assets.GetPathForMaterial(material)), type));
        return value;
    }

    void ApplyMaterialList(AssetSystem& assets, AssetType type, void* field,
                           const AssetFieldValue& value)
    {
        // Load each member up front so the materials are retained before the old
        // set is released below.
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

        const MaterialSetHandle old = MaterialSetHandle::FromToken(ReadHandleToken(field));
        WriteHandleToken(field, next.ToToken());
        assets.ReleaseMaterialSet(old);
    }
}

AssetFieldValue ReadAssetField(AssetSystem& assets, AssetType type,
                               AssetArity arity, const void* field)
{
    if (arity == AssetArity::Single && assets.SupportsAssetHandleOps(type))
        return ReadSingleHandle(assets, type, field);

    if (type == AssetType::Material && arity == AssetArity::List)
        return ReadMaterialList(assets, type, field);

    throw std::runtime_error(
        "ReadAssetField: unsupported (asset type " + std::to_string(static_cast<int>(type))
        + ", arity " + std::to_string(static_cast<int>(arity)) + ") shape");
}

void ApplyAssetField(AssetSystem& assets, AssetType type, AssetArity arity,
                     void* field, const AssetFieldValue& value)
{
    if (arity == AssetArity::Single && assets.SupportsAssetHandleOps(type))
    {
        ApplySingleHandle(assets, type, field, value);
        return;
    }

    if (type == AssetType::Material && arity == AssetArity::List)
    {
        ApplyMaterialList(assets, type, field, value);
        return;
    }

    throw std::runtime_error(
        "ApplyAssetField: unsupported (asset type " + std::to_string(static_cast<int>(type))
        + ", arity " + std::to_string(static_cast<int>(arity)) + ") shape");
}
