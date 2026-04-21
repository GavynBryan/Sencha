#include <core/assets/AssetSystem.h>

#include <core/logging/LoggingProvider.h>

#include <render/MaterialCache.h>
#include <render/static_mesh/StaticMeshCache.h>

#include <cassert>

AssetSystem::AssetSystem(LoggingProvider& logging,
                         AssetRegistry& registry,
                         StaticMeshCache& meshes,
                         MaterialCache& materials)
    : Log(logging.GetLogger<AssetSystem>())
    , Registry(registry)
    , StaticMeshes(&meshes)
    , Materials(&materials)
    , StaticMeshFileLoader(logging)
{
}

AssetSystem::AssetSystem(LoggingProvider& logging,
                         AssetRegistry& registry,
                         StaticMeshCache* meshes,
                         MaterialCache* materials)
    : Log(logging.GetLogger<AssetSystem>())
    , Registry(registry)
    , StaticMeshes(meshes)
    , Materials(materials)
    , StaticMeshFileLoader(logging)
{
}

StaticMeshHandle AssetSystem::RegisterProceduralStaticMesh(std::string_view path, StaticMeshData mesh)
{
    if (!IsValidAssetPath(path))
    {
        Log.Error("AssetSystem: invalid procedural static mesh asset path: {}", path);
        assert(false && "Invalid procedural static mesh asset path");
        return {};
    }

    AssetRecord record{
        .Type = AssetType::StaticMesh,
        .SourceKind = AssetSourceKind::Procedural,
        .Path = std::string(path),
        .FilePath = "",
    };

    if (!Registry.RegisterOrVerify(record))
    {
        Log.Error("AssetSystem: failed to register procedural static mesh asset: {}", record.Path);
        assert(false && "Failed to register procedural static mesh asset");
        return {};
    }

    if (!StaticMeshes)
    {
        Log.Error("AssetSystem: missing StaticMeshCache for procedural static mesh asset {}", record.Path);
        assert(false && "Missing StaticMeshCache for procedural static mesh asset");
        return {};
    }

    StaticMeshHandle handle = StaticMeshes->CreateFromData(record.Path, mesh);
    if (!handle.IsValid())
    {
        Log.Error("AssetSystem: failed to create procedural static mesh runtime resource: {}", record.Path);
        assert(false && "Failed to create procedural static mesh runtime resource");
        return {};
    }

    return handle;
}

MaterialHandle AssetSystem::RegisterProceduralMaterial(std::string_view path, Material material)
{
    if (!IsValidAssetPath(path))
    {
        Log.Error("AssetSystem: invalid procedural material asset path: {}", path);
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
        Log.Error("AssetSystem: failed to register procedural material asset: {}", record.Path);
        assert(false && "Failed to register procedural material asset");
        return {};
    }

    if (!Materials)
    {
        Log.Error("AssetSystem: missing MaterialCache for procedural material asset {}", record.Path);
        assert(false && "Missing MaterialCache for procedural material asset");
        return {};
    }

    MaterialHandle handle = Materials->Register(record.Path, material);
    if (!handle.IsValid())
    {
        Log.Error("AssetSystem: failed to create procedural material runtime resource: {}", record.Path);
        assert(false && "Failed to create procedural material runtime resource");
        return {};
    }

    return handle;
}

std::string_view AssetSystem::GetPathForStaticMesh(StaticMeshHandle handle) const
{
    return StaticMeshes ? StaticMeshes->GetName(handle) : std::string_view{};
}

std::string_view AssetSystem::GetPathForMaterial(MaterialHandle handle) const
{
    return Materials ? Materials->GetName(handle) : std::string_view{};
}

const AssetRecord* AssetSystem::Resolve(std::string_view path, AssetType expectedType) const
{
    if (path.empty())
    {
        Log.Error("AssetSystem: empty asset path");
        return nullptr;
    }

    const AssetRecord* record = Registry.FindByPath(path);
    if (!record)
    {
        Log.Error("AssetSystem: failed to resolve asset '{}'", path);
        return nullptr;
    }

    if (record->Type != expectedType)
    {
        Log.Error("AssetSystem: expected {} asset, got {} for path {}",
                  AssetTypeToString(expectedType),
                  AssetTypeToString(record->Type),
                  record->Path);
        return nullptr;
    }

    return record;
}

StaticMeshHandle AssetSystem::LoadStaticMesh(std::string_view path)
{
    const AssetRecord* record = Resolve(path, AssetType::StaticMesh);
    if (!record)
        return {};

    switch (record->SourceKind)
    {
    case AssetSourceKind::Procedural:
    {
        if (!StaticMeshes)
        {
            Log.Error("AssetSystem: missing StaticMeshCache for static mesh asset {}", record->Path);
            return {};
        }

        StaticMeshHandle handle = StaticMeshes->Acquire(record->Path);
        if (!handle.IsValid())
        {
            Log.Error("AssetSystem: static mesh cache has no runtime resource for path {}", record->Path);
            return {};
        }

        return handle;
    }
    case AssetSourceKind::File:
    {
        if (!StaticMeshes)
        {
            Log.Error("AssetSystem: missing StaticMeshCache for static mesh asset {}", record->Path);
            return {};
        }

        if (StaticMeshHandle existing = StaticMeshes->Acquire(record->Path); existing.IsValid())
            return existing;

        StaticMeshData mesh;
        const std::string_view filePath =
            record->FilePath.empty() ? std::string_view(record->Path) : std::string_view(record->FilePath);
        if (!StaticMeshFileLoader.LoadFromFile(filePath, mesh))
            return {};

        StaticMeshHandle handle = StaticMeshes->CreateFromData(record->Path, mesh);
        if (!handle.IsValid())
        {
            Log.Error("AssetSystem: failed to upload static mesh '{}'", filePath);
            return {};
        }

        return handle;
    }
    case AssetSourceKind::Generated:
        Log.Error("AssetSystem: generated static mesh loading not implemented: {}", record->Path);
        return {};
    case AssetSourceKind::Embedded:
        Log.Error("AssetSystem: embedded static mesh loading not implemented: {}", record->Path);
        return {};
    default:
        Log.Error("AssetSystem: unknown static mesh source kind for path {}", record->Path);
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
            Log.Error("AssetSystem: missing MaterialCache for material asset {}", record->Path);
            return {};
        }

        MaterialHandle handle = Materials->Acquire(record->Path);
        if (!handle.IsValid())
        {
            Log.Error("AssetSystem: material cache has no runtime resource for path {}", record->Path);
            return {};
        }

        return handle;
    }
    case AssetSourceKind::File:
        Log.Error("AssetSystem: .smat file loading not implemented: {}",
                  record->FilePath.empty() ? record->Path : record->FilePath);
        return {};
    case AssetSourceKind::Generated:
        Log.Error("AssetSystem: generated material loading not implemented: {}", record->Path);
        return {};
    case AssetSourceKind::Embedded:
        Log.Error("AssetSystem: embedded material loading not implemented: {}", record->Path);
        return {};
    default:
        Log.Error("AssetSystem: unknown material source kind for path {}", record->Path);
        return {};
    }
}
