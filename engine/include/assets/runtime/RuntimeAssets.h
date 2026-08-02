#pragma once

#include <anim/AnimationClipCache.h>
#include <anim/SkeletonCache.h>
#include <assets/data/DataAssetCache.h>
#include <assets/data/DataAssetLoader.h>
#include <assets/data/DataAssetTypeRegistry.h>
#include <audio/AudioClipCache.h>
#include <core/metadata/DataSchema.h>
#include <core/assets/AssetRegistry.h>
#include <assets/runtime/AssetSystem.h>
#include <graphics/vulkan/TextureCache.h>
#include <render/MaterialCache.h>
#include <render/MaterialSetCache.h>
#include <render/skinned_mesh/SkinnedMeshCache.h>
#include <render/static_mesh/StaticMeshCache.h>

class LoggingProvider;
class VulkanBufferService;
class VulkanDescriptorCache;
class VulkanImageService;
class VulkanSamplerCache;

// Declaration order is load-bearing — caches that hold RAII references into
// other caches must be declared after them so they are destroyed first:
//   Materials → Textures (so Textures is declared first),
//   StaticMeshes/SkinnedMeshes → Skeletons and AnimationClips → Skeletons (so
//   Skeletons is declared before all three, destroyed after them, Stage 5).
struct RuntimeAssets
{
    AssetRegistry Registry;
    TextureCache Textures;
    MaterialCache Materials;
    // After Materials so it is destroyed first: a set releases a reference to
    // each member material on teardown, which must outlive it.
    MaterialSetCache MaterialSets;
    SkeletonCache Skeletons;
    StaticMeshCache StaticMeshes;
    SkinnedMeshCache SkinnedMeshes;
    AnimationClipCache AnimationClips;
    AudioClipCache AudioClips;

    // Structured data. The subtype registry and schemas are separate from the
    // cache because a game module registers into them while the module is
    // mapped, and must unregister before it is unmapped.
    DataAssetTypeRegistry DataTypes;
    DataSchemaRegistry DataSchemas;
    DataAssetCache DataAssets;
    DataAssetLoader DataLoader;

    AssetSystem Assets;

    RuntimeAssets(LoggingProvider& logging,
                  VulkanBufferService& buffers,
                  VulkanImageService& images,
                  VulkanDescriptorCache& descriptors,
                  VulkanSamplerCache& samplers)
        : Registry(logging)
        , Textures(logging, images, descriptors, samplers)
        , Materials()
        , MaterialSets(&Materials)
        , Skeletons()
        , StaticMeshes(logging, buffers)
        , SkinnedMeshes(logging, buffers)
        , AnimationClips()
        , AudioClips(logging)
        , DataTypes()
        , DataSchemas()
        , DataAssets()
        , DataLoader(logging, &DataTypes, &DataSchemas, &DataAssets)
        , Assets(logging, Registry, StaticMeshes, Materials, Textures, AudioClips,
                 Skeletons, AnimationClips, SkinnedMeshes, MaterialSets)
    {
        // Unregistering a subtype with values still resident would leave the
        // cache holding a value nothing can interpret.
        DataTypes.SetResidentQuery([this](std::string_view typeName)
        {
            return DataAssets.HasResidentSubtype(typeName);
        });

        // Data is the one built-in kind AssetSystem cannot register itself:
        // its cache and loader live here, not in the front door.
        AssetKindRegistration data = MakeBuiltinAssetKind(AssetType::Data);
        data.Stager = &DataLoader;
        data.Store = &DataAssets;
        data.Commit = [this](AssetStaging&& staged) -> AssetLease
        {
            const DataAssetHandle handle = DataLoader.CommitTyped(std::move(staged));
            if (!handle.IsValid())
                return {};
            return AssetLease::Adopt(AssetType::Data, DataAssets, handle.ToToken());
        };
        data.Reload = [this](AssetStaging&& staged)
        {
            return DataLoader.CommitReload(std::move(staged));
        };
        (void)Assets.Kinds().Register(std::move(data));
    }

    RuntimeAssets(const RuntimeAssets&) = delete;
    RuntimeAssets& operator=(const RuntimeAssets&) = delete;

    RuntimeAssets(RuntimeAssets&&) = delete;
    RuntimeAssets& operator=(RuntimeAssets&&) = delete;
};
