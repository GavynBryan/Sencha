#include <assets/runtime/AssetSystem.h>

#include <core/logging/LoggingProvider.h>

#include <render/skinned_mesh/SkinnedMeshCache.h>
#include <render/static_mesh/StaticMeshCache.h>

#include <cassert>
#include <utility>

// Static and skinned mesh loads. Both resolve through the same record
// lookup and differ only in which cache and loader they commit to.

StaticMeshHandle AssetSystem::RegisterProceduralStaticMesh(std::string_view path, MeshGeometry mesh)
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

std::string_view AssetSystem::GetPathForStaticMesh(StaticMeshHandle handle) const
{
    return StaticMeshes ? StaticMeshes->GetName(handle) : std::string_view{};
}

std::string_view AssetSystem::GetPathForSkinnedMesh(SkinnedMeshHandle handle) const
{
    return SkinnedMeshes ? SkinnedMeshes->GetName(handle) : std::string_view{};
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
    // Cooked artifacts (including the level cook's brush bake) register as
    // File: RegisterCookedAssets points the record's FilePath at the .cooked/
    // artifact and the loader reads it through the same byte source.
    case AssetSourceKind::File:
    {
        if (!StaticMeshes)
        {
            Log.Error("AssetSystem: missing StaticMeshCache for static mesh asset {}", record->Path);
            return {};
        }

        if (StaticMeshHandle existing = StaticMeshes->Acquire(record->Path); existing.IsValid())
            return existing;

        AssetStaging staging = MeshLoader.LoadStaged(*record, Source);
        if (!staging.IsValid())
        {
            Log.Error("AssetSystem: {}", staging.Error);
            return {};
        }

        return MeshLoader.CommitTyped(std::move(staging));
    }
    default:
        Log.Error("AssetSystem: unknown static mesh source kind for path {}", record->Path);
        return {};
    }
}

SkinnedMeshHandle AssetSystem::LoadSkinnedMesh(std::string_view path)
{
    const AssetRecord* record = Resolve(path, AssetType::SkinnedMesh);
    if (!record)
        return {};

    if (!SkinnedMeshes)
    {
        Log.Error("AssetSystem: missing SkinnedMeshCache for skinned mesh asset {}", record->Path);
        return {};
    }

    if (SkinnedMeshHandle existing = SkinnedMeshes->Acquire(record->Path); existing.IsValid())
        return existing;

    if (record->SourceKind != AssetSourceKind::File)
    {
        Log.Error("AssetSystem: skinned mesh cache has no runtime resource for path {}", record->Path);
        return {};
    }

    AssetStaging staging = SkinnedLoader.LoadStaged(*record, Source);
    if (!staging.IsValid())
    {
        Log.Error("AssetSystem: {}", staging.Error);
        return {};
    }

    return SkinnedLoader.CommitTyped(std::move(staging));
}

StaticMeshHandle AssetSystem::TryAcquireStaticMesh(std::string_view path)
{
    return StaticMeshes ? StaticMeshes->Acquire(path) : StaticMeshHandle{};
}

SkinnedMeshHandle AssetSystem::TryAcquireSkinnedMesh(std::string_view path)
{
    return SkinnedMeshes ? SkinnedMeshes->Acquire(path) : SkinnedMeshHandle{};
}

void AssetSystem::ReleaseStaticMesh(StaticMeshHandle handle)
{
    if (StaticMeshes)
        StaticMeshes->Release(handle);
}

void AssetSystem::ReleaseSkinnedMesh(SkinnedMeshHandle handle)
{
    if (SkinnedMeshes)
        SkinnedMeshes->Release(handle);
}
