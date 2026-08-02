#include <assets/runtime/AssetSystem.h>

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

IAssetStager* AssetSystem::LoaderFor(AssetType type)
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
