#pragma once

#include <anim/AnimationClipCache.h>
#include <anim/SkeletonCache.h>
#include <audio/AudioClipCache.h>
#include <core/assets/AssetRegistry.h>
#include <core/assets/AssetSystem.h>
#include <graphics/vulkan/TextureCache.h>
#include <render/MaterialCache.h>
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
    SkeletonCache Skeletons;
    StaticMeshCache StaticMeshes;
    SkinnedMeshCache SkinnedMeshes;
    AnimationClipCache AnimationClips;
    AudioClipCache AudioClips;
    AssetSystem Assets;

    RuntimeAssets(LoggingProvider& logging,
                  VulkanBufferService& buffers,
                  VulkanImageService& images,
                  VulkanDescriptorCache& descriptors,
                  VulkanSamplerCache& samplers)
        : Registry(logging)
        , Textures(logging, images, descriptors, samplers)
        , Materials()
        , Skeletons()
        , StaticMeshes(logging, buffers)
        , SkinnedMeshes(logging, buffers)
        , AnimationClips()
        , AudioClips(logging)
        , Assets(logging, Registry, StaticMeshes, Materials, Textures, AudioClips,
                 Skeletons, AnimationClips, SkinnedMeshes)
    {
    }

    RuntimeAssets(const RuntimeAssets&) = delete;
    RuntimeAssets& operator=(const RuntimeAssets&) = delete;

    RuntimeAssets(RuntimeAssets&&) = delete;
    RuntimeAssets& operator=(RuntimeAssets&&) = delete;
};
