#include <core/assets/AssetSystem.h>

#include <core/logging/LoggingProvider.h>

#include <anim/AnimationClipCache.h>
#include <anim/SkeletonCache.h>
#include <audio/AudioClipCache.h>
#include <graphics/vulkan/TextureCache.h>
#include <render/MaterialCache.h>
#include <render/MaterialSetCache.h>
#include <render/skinned_mesh/SkinnedMeshCache.h>
#include <render/static_mesh/StaticMeshCache.h>

#include <cassert>
#include <utility>

AssetSystem::AssetSystem(LoggingProvider& logging,
                         AssetRegistry& registry,
                         StaticMeshCache& meshes,
                         MaterialCache& materials,
                         TextureCache& textures,
                         AudioClipCache& audioClips,
                         SkeletonCache& skeletons,
                         AnimationClipCache& animationClips,
                         SkinnedMeshCache& skinnedMeshes,
                         MaterialSetCache& materialSets)
    : AssetSystem(logging, registry, &meshes, &materials, &textures, &audioClips,
                  &skeletons, &animationClips, &skinnedMeshes, &materialSets)
{
}

AssetSystem::AssetSystem(LoggingProvider& logging,
                         AssetRegistry& registry,
                         StaticMeshCache* meshes,
                         MaterialCache* materials,
                         TextureCache* textures,
                         AudioClipCache* audioClips,
                         SkeletonCache* skeletons,
                         AnimationClipCache* animationClips,
                         SkinnedMeshCache* skinnedMeshes,
                         MaterialSetCache* materialSets)
    : Log(logging.GetLogger<AssetSystem>())
    , Registry(registry)
    , StaticMeshes(meshes)
    , Materials(materials)
    , MaterialSets(materialSets)
    , Textures(textures)
    , AudioClips(audioClips)
    , Skeletons(skeletons)
    , AnimationClips(animationClips)
    , SkinnedMeshes(skinnedMeshes)
    , MeshLoader(logging, meshes)
    , TexLoader(logging, textures)
    , MatLoader(logging, *this, materials, textures)
    , ClipLoader(logging, audioClips)
    , SkelLoader(logging, skeletons)
    , AnimLoader(logging, *this, animationClips, skeletons)
    , SkinnedLoader(logging, *this, skinnedMeshes, skeletons)
{
}

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

std::string_view AssetSystem::GetPathForSkinnedMesh(SkinnedMeshHandle handle) const
{
    return SkinnedMeshes ? SkinnedMeshes->GetName(handle) : std::string_view{};
}

std::string_view AssetSystem::GetPathForAudioClip(AudioClipHandle handle) const
{
    return AudioClips ? AudioClips->GetName(handle) : std::string_view{};
}

std::string_view AssetSystem::GetPathForSkeleton(SkeletonHandle handle) const
{
    return Skeletons ? Skeletons->GetName(handle) : std::string_view{};
}

std::string_view AssetSystem::GetPathForAnimationClip(AnimationClipHandle handle) const
{
    return AnimationClips ? AnimationClips->GetName(handle) : std::string_view{};
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

bool AssetSystem::IsResident(std::string_view path, AssetType type) const
{
    switch (type)
    {
    case AssetType::StaticMesh:    return StaticMeshes && StaticMeshes->Find(path).IsValid();
    case AssetType::SkinnedMesh:   return SkinnedMeshes && SkinnedMeshes->Find(path).IsValid();
    case AssetType::Material:      return Materials && Materials->Find(path).IsValid();
    case AssetType::Texture:       return Textures && Textures->Find(path).IsValid();
    case AssetType::Audio:         return AudioClips && AudioClips->Find(path).IsValid();
    case AssetType::Skeleton:      return Skeletons && Skeletons->Find(path).IsValid();
    case AssetType::AnimationClip: return AnimationClips && AnimationClips->Find(path).IsValid();
    default:                       return false;
    }
}

std::string_view AssetSystem::ResolveRefPath(AssetId id,
                                             std::string_view fallbackPath,
                                             AssetType expectedType) const
{
    if (!id.IsValid())
        return fallbackPath;

    const AssetRecord* record = Registry.FindById(id);
    if (record == nullptr)
        return fallbackPath;

    if (record->Type != expectedType)
    {
        Log.Error("AssetSystem: id {} is a {} asset, expected {}; falling back to path '{}'",
                  AssetIdToString(id),
                  AssetTypeToString(record->Type),
                  AssetTypeToString(expectedType),
                  fallbackPath);
        return fallbackPath;
    }

    return record->Path;
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
    // Generated meshes (the level cook's brush bake, 05-) load byte-for-byte like
    // File: the record's FilePath points at the .cooked/ artifact and the loader
    // reads it through the same byte source. The kind differs only in provenance —
    // a Generated asset is owned by a level source and is the prune pass's to reap.
    case AssetSourceKind::File:
    case AssetSourceKind::Generated:
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
    case AssetSourceKind::Embedded:
        Log.Error("AssetSystem: embedded static mesh loading not implemented: {}", record->Path);
        return {};
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

        AssetStaging staging = TexLoader.LoadStaged(*record, Source, srgb);
        if (!staging.IsValid())
        {
            Log.Error("AssetSystem: {}", staging.Error);
            return {};
        }

        return TexLoader.CommitTyped(std::move(staging));
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


AudioClipHandle AssetSystem::LoadAudioClip(std::string_view path)
{
    const AssetRecord* record = Resolve(path, AssetType::Audio);
    if (!record)
        return {};

    if (!AudioClips)
    {
        Log.Error("AssetSystem: missing AudioClipCache for audio asset {}", record->Path);
        return {};
    }

    switch (record->SourceKind)
    {
    case AssetSourceKind::Procedural:
    {
        AudioClipHandle handle = AudioClips->Acquire(record->Path);
        if (!handle.IsValid())
        {
            Log.Error("AssetSystem: audio clip cache has no runtime resource for path {}", record->Path);
            return {};
        }

        return handle;
    }
    case AssetSourceKind::File:
    {
        if (AudioClipHandle existing = AudioClips->Acquire(record->Path); existing.IsValid())
            return existing;

        AssetStaging staging = ClipLoader.LoadStaged(*record, Source);
        if (!staging.IsValid())
        {
            Log.Error("AssetSystem: {}", staging.Error);
            return {};
        }

        return ClipLoader.CommitTyped(std::move(staging));
    }
    case AssetSourceKind::Generated:
        Log.Error("AssetSystem: generated audio clip loading not implemented: {}", record->Path);
        return {};
    case AssetSourceKind::Embedded:
        Log.Error("AssetSystem: embedded audio clip loading not implemented: {}", record->Path);
        return {};
    default:
        Log.Error("AssetSystem: unknown audio clip source kind for path {}", record->Path);
        return {};
    }
}

SkeletonHandle AssetSystem::LoadSkeleton(std::string_view path)
{
    const AssetRecord* record = Resolve(path, AssetType::Skeleton);
    if (!record)
        return {};

    if (!Skeletons)
    {
        Log.Error("AssetSystem: missing SkeletonCache for skeleton asset {}", record->Path);
        return {};
    }

    if (SkeletonHandle existing = Skeletons->Acquire(record->Path); existing.IsValid())
        return existing;

    if (record->SourceKind != AssetSourceKind::File)
    {
        Log.Error("AssetSystem: skeleton cache has no runtime resource for path {}", record->Path);
        return {};
    }

    AssetStaging staging = SkelLoader.LoadStaged(*record, Source);
    if (!staging.IsValid())
    {
        Log.Error("AssetSystem: {}", staging.Error);
        return {};
    }

    return SkelLoader.CommitTyped(std::move(staging));
}

AnimationClipHandle AssetSystem::LoadAnimationClip(std::string_view path)
{
    const AssetRecord* record = Resolve(path, AssetType::AnimationClip);
    if (!record)
        return {};

    if (!AnimationClips)
    {
        Log.Error("AssetSystem: missing AnimationClipCache for animation asset {}", record->Path);
        return {};
    }

    if (AnimationClipHandle existing = AnimationClips->Acquire(record->Path); existing.IsValid())
        return existing;

    if (record->SourceKind != AssetSourceKind::File)
    {
        Log.Error("AssetSystem: animation clip cache has no runtime resource for path {}", record->Path);
        return {};
    }

    AssetStaging staging = AnimLoader.LoadStaged(*record, Source);
    if (!staging.IsValid())
    {
        Log.Error("AssetSystem: {}", staging.Error);
        return {};
    }

    return AnimLoader.CommitTyped(std::move(staging));
}

StaticMeshHandle AssetSystem::TryAcquireStaticMesh(std::string_view path)
{
    return StaticMeshes ? StaticMeshes->Acquire(path) : StaticMeshHandle{};
}

SkinnedMeshHandle AssetSystem::TryAcquireSkinnedMesh(std::string_view path)
{
    return SkinnedMeshes ? SkinnedMeshes->Acquire(path) : SkinnedMeshHandle{};
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

AudioClipHandle AssetSystem::TryAcquireAudioClip(std::string_view path)
{
    return AudioClips ? AudioClips->Acquire(path) : AudioClipHandle{};
}

SkeletonHandle AssetSystem::TryAcquireSkeleton(std::string_view path)
{
    return Skeletons ? Skeletons->Acquire(path) : SkeletonHandle{};
}

AnimationClipHandle AssetSystem::TryAcquireAnimationClip(std::string_view path)
{
    return AnimationClips ? AnimationClips->Acquire(path) : AnimationClipHandle{};
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

void AssetSystem::ReleaseAudioClip(AudioClipHandle handle)
{
    if (AudioClips)
        AudioClips->Release(handle);
}

void AssetSystem::ReleaseSkeleton(SkeletonHandle handle)
{
    if (Skeletons)
        Skeletons->Release(handle);
}

void AssetSystem::ReleaseAnimationClip(AnimationClipHandle handle)
{
    if (AnimationClips)
        AnimationClips->Release(handle);
}

IAssetLoader* AssetSystem::LoaderFor(AssetType type)
{
    switch (type)
    {
    case AssetType::StaticMesh:    return &MeshLoader;
    case AssetType::SkinnedMesh:   return &SkinnedLoader;
    case AssetType::Texture:       return &TexLoader;
    case AssetType::Material:      return &MatLoader;
    case AssetType::Audio:         return &ClipLoader;
    case AssetType::Skeleton:      return &SkelLoader;
    case AssetType::AnimationClip: return &AnimLoader;
    default:                       return nullptr;
    }
}
