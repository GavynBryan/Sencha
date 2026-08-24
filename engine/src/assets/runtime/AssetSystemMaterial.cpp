#include <assets/runtime/AssetSystem.h>

#include <core/logging/LoggingProvider.h>

#include <assets/texture/TextureCache.h>
#include <render/MaterialCache.h>
#include <render/MaterialSetCache.h>

#include <cassert>
#include <utility>

// Materials, textures, and material sets. Grouped because a material load
// pulls its textures in the same pass, and a material set is just a
// deduplicated span of material handles.

MaterialSetHandle AssetSystem::AcquireMaterialSet(std::span<const MaterialHandle> materials)
{
    if (!MaterialSets)
    {
        Log.Error("AssetSystem: missing MaterialSetCache for material set acquire");
        return {};
    }
    return MaterialSets->Acquire(materials);
}

const std::vector<MaterialHandle>* AssetSystem::GetMaterialSet(MaterialSetHandle handle) const
{
    return MaterialSets ? MaterialSets->Get(handle) : nullptr;
}

void AssetSystem::ReleaseMaterialSet(MaterialSetHandle handle)
{
    if (MaterialSets)
        MaterialSets->Release(handle);
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

std::string_view AssetSystem::GetPathForTexture(TextureHandle handle) const
{
    return Textures ? Textures->GetName(handle) : std::string_view{};
}

std::string_view AssetSystem::GetPathForMaterial(MaterialHandle handle) const
{
    return Materials ? Materials->GetName(handle) : std::string_view{};
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
    {
        if (!Materials)
        {
            Log.Error("AssetSystem: missing MaterialCache for material asset {}", record->Path);
            return {};
        }

        if (MaterialHandle existing = Materials->Acquire(record->Path); existing.IsValid())
            return existing;

        AssetStaging staging = MatLoader.LoadStaged(*record, Source);
        if (!staging.IsValid())
        {
            Log.Error("AssetSystem: failed to load material '{}': {}", record->Path, staging.Error);
            return {};
        }

        return MatLoader.CommitTyped(std::move(staging));
    }
    default:
        Log.Error("AssetSystem: unknown material source kind for path {}", record->Path);
        return {};
    }
}

TextureHandle AssetSystem::LoadTexture(std::string_view path, bool srgb)
{
    const AssetRecord* record = Resolve(path, AssetType::Texture);
    if (!record)
        return {};

    if (!Textures)
    {
        Log.Error("AssetSystem: missing TextureCache for texture asset {}", record->Path);
        return {};
    }

    switch (record->SourceKind)
    {
    case AssetSourceKind::Procedural:
    {
        TextureHandle existing = Textures->Find(record->Path);
        if (!existing.IsValid())
        {
            Log.Error("AssetSystem: texture cache has no runtime resource for path {}", record->Path);
            return {};
        }

        Textures->Retain(existing);
        return existing;
    }
    case AssetSourceKind::File:
    {
        if (TextureHandle existing = Textures->Find(record->Path); existing.IsValid())
        {
            Textures->Retain(existing);
            return existing;
        }

        AssetStaging staging = TexLoader.LoadStaged(*record, Source, srgb);
        if (!staging.IsValid())
        {
            Log.Error("AssetSystem: {}", staging.Error);
            return {};
        }

        return TexLoader.CommitTyped(std::move(staging));
    }
    default:
        Log.Error("AssetSystem: unknown texture source kind for path {}", record->Path);
        return {};
    }
}

MaterialHandle AssetSystem::TryAcquireMaterial(std::string_view path)
{
    return Materials ? Materials->Acquire(path) : MaterialHandle{};
}

TextureHandle AssetSystem::TryAcquireTexture(std::string_view path)
{
    if (!Textures)
        return {};

    TextureHandle handle = Textures->Find(path);
    if (handle.IsValid())
        Textures->Retain(handle);
    return handle;
}

void AssetSystem::ReleaseMaterial(MaterialHandle handle)
{
    if (Materials)
        Materials->Release(handle);
}

void AssetSystem::ReleaseTexture(TextureHandle handle)
{
    if (Textures)
        Textures->Release(handle);
}
