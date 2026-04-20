#include <core/assets/AssetSystem.h>

#include <render/MaterialCache.h>
#include <render/MeshCache.h>

#include <cassert>
#include <cstdio>

AssetSystem::AssetSystem(AssetRegistry& registry,
                         MeshCache& meshes,
                         MaterialCache& materials)
    : Registry(registry)
    , Meshes(&meshes)
    , Materials(&materials)
{
}

AssetSystem::AssetSystem(AssetRegistry& registry,
                         MeshCache* meshes,
                         MaterialCache* materials)
    : Registry(registry)
    , Meshes(meshes)
    , Materials(materials)
{
}

MeshHandle AssetSystem::RegisterProceduralMesh(std::string_view path, MeshData mesh)
{
    if (!IsValidAssetPath(path))
    {
        std::fprintf(stderr, "AssetSystem: invalid procedural mesh asset path: %.*s\n",
            static_cast<int>(path.size()), path.data());
        assert(false && "Invalid procedural mesh asset path");
        return {};
    }

    AssetRecord record{
        .Type = AssetType::Mesh,
        .SourceKind = AssetSourceKind::Procedural,
        .Path = std::string(path),
        .FilePath = "",
    };

    if (!Registry.RegisterOrVerify(record))
    {
        std::fprintf(stderr, "AssetSystem: failed to register procedural mesh asset: %s\n",
            record.Path.c_str());
        assert(false && "Failed to register procedural mesh asset");
        return {};
    }

    if (!Meshes)
    {
        std::fprintf(stderr, "AssetSystem: missing MeshCache for procedural mesh asset %s\n",
            record.Path.c_str());
        assert(false && "Missing MeshCache for procedural mesh asset");
        return {};
    }

    MeshHandle handle = Meshes->CreateFromData(record.Path, mesh);
    if (!handle.IsValid())
    {
        std::fprintf(stderr, "AssetSystem: failed to create procedural mesh runtime resource: %s\n",
            record.Path.c_str());
        assert(false && "Failed to create procedural mesh runtime resource");
        return {};
    }

    return handle;
}

MaterialHandle AssetSystem::RegisterProceduralMaterial(std::string_view path, Material material)
{
    if (!IsValidAssetPath(path))
    {
        std::fprintf(stderr, "AssetSystem: invalid procedural material asset path: %.*s\n",
            static_cast<int>(path.size()), path.data());
        assert(false && "Invalid procedural material asset path");
        return {};
    }

    AssetRecord record{
        .Type = AssetType::Material,
        .SourceKind = AssetSourceKind::Procedural,
        .Path = std::string(path),
        .FilePath = "",
    };

    if (!Registry.RegisterOrVerify(record))
    {
        std::fprintf(stderr, "AssetSystem: failed to register procedural material asset: %s\n",
            record.Path.c_str());
        assert(false && "Failed to register procedural material asset");
        return {};
    }

    if (!Materials)
    {
        std::fprintf(stderr, "AssetSystem: missing MaterialCache for procedural material asset %s\n",
            record.Path.c_str());
        assert(false && "Missing MaterialCache for procedural material asset");
        return {};
    }

    MaterialHandle handle = Materials->Register(record.Path, material);
    if (!handle.IsValid())
    {
        std::fprintf(stderr, "AssetSystem: failed to create procedural material runtime resource: %s\n",
            record.Path.c_str());
        assert(false && "Failed to create procedural material runtime resource");
        return {};
    }

    return handle;
}

std::string_view AssetSystem::GetPathForMesh(MeshHandle handle) const
{
    return Meshes ? Meshes->GetName(handle) : std::string_view{};
}

std::string_view AssetSystem::GetPathForMaterial(MaterialHandle handle) const
{
    return Materials ? Materials->GetName(handle) : std::string_view{};
}

const AssetRecord* AssetSystem::Resolve(std::string_view path, AssetType expectedType) const
{
    if (path.empty())
    {
        std::fprintf(stderr, "AssetSystem: empty asset path\n");
        return nullptr;
    }

    const AssetRecord* record = Registry.FindByPath(path);
    if (!record)
    {
        std::fprintf(stderr, "AssetSystem: asset path not registered: %.*s\n",
            static_cast<int>(path.size()), path.data());
        return nullptr;
    }

    if (record->Type != expectedType)
    {
        std::fprintf(stderr, "AssetSystem: expected %.*s asset, got %.*s for path %s\n",
            static_cast<int>(AssetTypeToString(expectedType).size()),
            AssetTypeToString(expectedType).data(),
            static_cast<int>(AssetTypeToString(record->Type).size()),
            AssetTypeToString(record->Type).data(),
            record->Path.c_str());
        return nullptr;
    }

    return record;
}

MeshHandle AssetSystem::LoadMesh(std::string_view path)
{
    const AssetRecord* record = Resolve(path, AssetType::Mesh);
    if (!record)
        return {};

    switch (record->SourceKind)
    {
    case AssetSourceKind::Procedural:
    {
        if (!Meshes)
        {
            std::fprintf(stderr, "AssetSystem: missing MeshCache for mesh asset %s\n",
                record->Path.c_str());
            return {};
        }

        // Scene load currently acquires a runtime reference to this asset. Scene
        // unload/reload code is responsible for releasing those handles later.
        MeshHandle handle = Meshes->Acquire(record->Path);
        if (!handle.IsValid())
        {
            std::fprintf(stderr, "AssetSystem: mesh cache has no runtime resource for path %s\n",
                record->Path.c_str());
            return {};
        }

        return handle;
    }
    case AssetSourceKind::File:
        std::fprintf(stderr, "AssetSystem: .smesh file loading not implemented: %s\n",
            record->FilePath.empty() ? record->Path.c_str() : record->FilePath.c_str());
        return {};
    case AssetSourceKind::Generated:
        std::fprintf(stderr, "AssetSystem: generated mesh loading not implemented: %s\n",
            record->Path.c_str());
        return {};
    case AssetSourceKind::Embedded:
        std::fprintf(stderr, "AssetSystem: embedded mesh loading not implemented: %s\n",
            record->Path.c_str());
        return {};
    default:
        std::fprintf(stderr, "AssetSystem: unknown mesh source kind for path %s\n",
            record->Path.c_str());
        return {};
    }
}

MaterialHandle AssetSystem::LoadMaterial(std::string_view path)
{
    const AssetRecord* record = Resolve(path, AssetType::Material);
    if (!record)
        return {};

    switch (record->SourceKind)
    {
    case AssetSourceKind::Procedural:
    {
        if (!Materials)
        {
            std::fprintf(stderr, "AssetSystem: missing MaterialCache for material asset %s\n",
                record->Path.c_str());
            return {};
        }

        // Scene load currently acquires a runtime reference to this asset. Scene
        // unload/reload code is responsible for releasing those handles later.
        MaterialHandle handle = Materials->Acquire(record->Path);
        if (!handle.IsValid())
        {
            std::fprintf(stderr, "AssetSystem: material cache has no runtime resource for path %s\n",
                record->Path.c_str());
            return {};
        }

        return handle;
    }
    case AssetSourceKind::File:
        std::fprintf(stderr, "AssetSystem: .smat file loading not implemented: %s\n",
            record->FilePath.empty() ? record->Path.c_str() : record->FilePath.c_str());
        return {};
    case AssetSourceKind::Generated:
        std::fprintf(stderr, "AssetSystem: generated material loading not implemented: %s\n",
            record->Path.c_str());
        return {};
    case AssetSourceKind::Embedded:
        std::fprintf(stderr, "AssetSystem: embedded material loading not implemented: %s\n",
            record->Path.c_str());
        return {};
    default:
        std::fprintf(stderr, "AssetSystem: unknown material source kind for path %s\n",
            record->Path.c_str());
        return {};
    }
}
