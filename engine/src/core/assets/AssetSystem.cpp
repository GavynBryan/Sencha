#include <core/assets/AssetSystem.h>

#include <assets/material/MaterialLoader.h>
#include <core/logging/LoggingProvider.h>

#include <graphics/vulkan/TextureCache.h>
#include <render/ImageLoader.h>
#include <render/MaterialCache.h>
#include <render/static_mesh/StaticMeshCache.h>

#include <cassert>
#include <utility>

AssetSystem::AssetSystem(LoggingProvider& logging,
                         AssetRegistry& registry,
                         StaticMeshCache& meshes,
                         MaterialCache& materials,
                         TextureCache& textures)
    : Log(logging.GetLogger<AssetSystem>())
    , Registry(registry)
    , StaticMeshes(&meshes)
    , Materials(&materials)
    , Textures(&textures)
    , StaticMeshFileLoader(logging)
{
}

AssetSystem::AssetSystem(LoggingProvider& logging,
                         AssetRegistry& registry,
                         StaticMeshCache* meshes,
                         MaterialCache* materials,
                         TextureCache* textures)
    : Log(logging.GetLogger<AssetSystem>())
    , Registry(registry)
    , StaticMeshes(meshes)
    , Materials(materials)
    , Textures(textures)
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
    {
        if (!Materials)
        {
            Log.Error("AssetSystem: missing MaterialCache for material asset {}", record->Path);
            return {};
        }

        if (MaterialHandle existing = Materials->Acquire(record->Path); existing.IsValid())
            return existing;

        const std::string_view filePath =
            record->FilePath.empty() ? std::string_view(record->Path) : std::string_view(record->FilePath);

        MaterialDescription desc;
        MaterialParseError parseError;
        if (!LoadMaterialFromFile(filePath, desc, &parseError))
        {
            Log.Error("AssetSystem: failed to load material '{}': {}", filePath, parseError.Message);
            return {};
        }

        Material material;
        material.Pass = ShaderPassId::ForwardOpaque;
        material.BaseColor = desc.BaseColorFactor;
        material.EmissiveFactor = desc.EmissiveFactor;
        material.NormalScale = desc.NormalScale;
        material.RoughnessFactor = desc.RoughnessFactor;
        material.MetallicFactor = desc.MetallicFactor;
        material.AlphaMode = desc.AlphaMode;
        material.AlphaCutoff = desc.AlphaCutoff;

        if (material.AlphaMode == MaterialAlphaMode::Blend)
        {
            Log.Warn("AssetSystem: material '{}' uses alpha_mode 'blend'; "
                     "transparent phase not implemented, rendering opaque",
                     record->Path);
        }

        std::vector<TextureCacheHandle> ownedTextures;
        ResolveMaterialTextureSlot(desc.BaseColorTexture, /*srgb*/ true,
                                   material.BaseColorTextureIndex, ownedTextures);
        ResolveMaterialTextureSlot(desc.NormalTexture, /*srgb*/ false,
                                   material.NormalTextureIndex, ownedTextures);
        ResolveMaterialTextureSlot(desc.OrmTexture, /*srgb*/ false,
                                   material.OrmTextureIndex, ownedTextures);
        ResolveMaterialTextureSlot(desc.EmissiveTexture, /*srgb*/ true,
                                   material.EmissiveTextureIndex, ownedTextures);

        MaterialHandle handle = Materials->Register(record->Path, material, std::move(ownedTextures));
        if (!handle.IsValid())
        {
            Log.Error("AssetSystem: failed to create material runtime resource: {}", record->Path);
            return {};
        }

        return handle;
    }
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

        const std::string_view filePath =
            record->FilePath.empty() ? std::string_view(record->Path) : std::string_view(record->FilePath);

        std::optional<Image> image = LoadImageFromFile(filePath, srgb);
        if (!image)
        {
            Log.Error("AssetSystem: failed to load image '{}'", filePath);
            return {};
        }

        TextureHandle handle = Textures->CreateFromImage(record->Path, *image);
        if (!handle.IsValid())
            Log.Error("AssetSystem: failed to upload texture '{}'", filePath);

        return handle;
    }
    case AssetSourceKind::Generated:
        Log.Error("AssetSystem: generated texture loading not implemented: {}", record->Path);
        return {};
    case AssetSourceKind::Embedded:
        Log.Error("AssetSystem: embedded texture loading not implemented: {}", record->Path);
        return {};
    default:
        Log.Error("AssetSystem: unknown texture source kind for path {}", record->Path);
        return {};
    }
}

void AssetSystem::ResolveMaterialTextureSlot(const AssetRef& ref,
                                             bool srgb,
                                             uint32_t& outIndex,
                                             std::vector<TextureCacheHandle>& owned)
{
    if (!ref.IsValid())
        return;

    if (!Textures)
    {
        Log.Warn("AssetSystem: no TextureCache; texture '{}' left at neutral default", ref.Path);
        return;
    }

    TextureHandle handle = LoadTexture(ref.Path, srgb);
    if (!handle.IsValid())
    {
        Log.Error("AssetSystem: failed to resolve texture '{}'; using neutral default", ref.Path);
        return;
    }

    const BindlessImageIndex bindless = Textures->GetBindlessIndex(handle);
    if (!bindless.IsValid())
    {
        Log.Error("AssetSystem: texture '{}' has no bindless slot; using neutral default", ref.Path);
        Textures->Release(handle);
        return;
    }

    outIndex = bindless.Value;
    // LoadTexture already incremented the refcount -- wrap without attaching.
    owned.emplace_back(Textures, handle, TextureCacheHandle::NoAttachTag{});
}
