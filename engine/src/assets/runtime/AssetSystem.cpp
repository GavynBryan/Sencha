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
#include <functional>
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
    RegisterKinds();
}

namespace
{
    // Wraps a loader's CommitTyped so the lease adopts the creation reference
    // the commit already holds. TCache is the issuing store, which is what
    // decodes the token back into its own handle.
    template<typename TLoader, typename TCache>
    std::function<AssetLease(AssetStaging&&)> MakeCommit(AssetType type,
                                                         TLoader& loader,
                                                         TCache* cache)
    {
        if (cache == nullptr)
            return {};

        return [type, &loader, cache](AssetStaging&& staged) -> AssetLease
        {
            auto handle = loader.CommitTyped(std::move(staged));
            if (!handle.IsValid())
                return {};

            return AssetLease::Adopt(type, *cache, handle.ToToken());
        };
    }

    template<typename TLoader>
    std::function<bool(AssetStaging&&)> MakeReload(TLoader& loader)
    {
        return [&loader](AssetStaging&& staged) -> bool
        {
            return loader.CommitReload(std::move(staged));
        };
    }
}

void AssetSystem::RegisterKinds()
{
    // Identity comes from the built-in table; this only attaches the load half.
    // The stager is always wired because staging touches no cache; the commit
    // half appears only for caches the host actually supplied, so a headless
    // AssetSystem still stages and simply reports the commit as a failure.
    const auto attach = [this](AssetType type,
                               IAssetStager& stager,
                               IAssetStore* store,
                               std::function<AssetLease(AssetStaging&&)> commit,
                               std::function<bool(AssetStaging&&)> reload = {})
    {
        AssetKindRegistration registration = MakeBuiltinAssetKind(type);
        registration.Stager = &stager;
        if (store != nullptr && commit)
        {
            registration.Store = store;
            registration.Commit = std::move(commit);
            registration.Reload = std::move(reload);
        }

        if (!KindRegistry.Register(std::move(registration)))
            Log.Error("AssetSystem: rejected {} kind registration", AssetTypeToString(type));
    };

    attach(AssetType::StaticMesh, MeshLoader, StaticMeshes,
           MakeCommit(AssetType::StaticMesh, MeshLoader, StaticMeshes),
           MakeReload(MeshLoader));
    attach(AssetType::SkinnedMesh, SkinnedLoader, SkinnedMeshes,
           MakeCommit(AssetType::SkinnedMesh, SkinnedLoader, SkinnedMeshes));
    attach(AssetType::Material, MatLoader, Materials,
           MakeCommit(AssetType::Material, MatLoader, Materials),
           MakeReload(MatLoader));
    attach(AssetType::Texture, TexLoader, Textures,
           MakeCommit(AssetType::Texture, TexLoader, Textures),
           MakeReload(TexLoader));
    attach(AssetType::Audio, ClipLoader, AudioClips,
           MakeCommit(AssetType::Audio, ClipLoader, AudioClips));
    attach(AssetType::Skeleton, SkelLoader, Skeletons,
           MakeCommit(AssetType::Skeleton, SkelLoader, Skeletons));
    attach(AssetType::AnimationClip, AnimLoader, AnimationClips,
           MakeCommit(AssetType::AnimationClip, AnimLoader, AnimationClips));

    // Scenes are scanned and referenced but never resolved through a cache.
    if (!KindRegistry.Register(MakeBuiltinAssetKind(AssetType::Scene)))
        Log.Error("AssetSystem: rejected Scene kind registration");
}

IAssetStore* AssetSystem::StoreFor(AssetType type)
{
    AssetKindRegistration* kind = KindRegistry.Find(type);
    return kind ? kind->Store : nullptr;
}

const IAssetStore* AssetSystem::StoreFor(AssetType type) const
{
    const AssetKindRegistration* kind = KindRegistry.Find(type);
    return kind ? kind->Store : nullptr;
}

AssetLease AssetSystem::TryAcquireLease(std::string_view path, AssetType type)
{
    IAssetStore* store = StoreFor(type);
    return store ? store->TryAcquireLease(path) : AssetLease{};
}

AssetLease AssetSystem::Commit(AssetStaging&& staged)
{
    const AssetKindRegistration* kind = KindRegistry.Find(staged.Record.Type);
    if (kind == nullptr || !kind->Commit)
    {
        Log.Error("AssetSystem: no commit registered for {} '{}'",
                  AssetTypeToString(staged.Record.Type), staged.Record.Path);
        return {};
    }

    return kind->Commit(std::move(staged));
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
    const IAssetStore* store = StoreFor(type);
    return store != nullptr && store->IsResident(path);
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
    AssetKindRegistration* kind = KindRegistry.Find(type);
    return kind ? kind->Stager : nullptr;
}
