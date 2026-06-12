#pragma once

#include <audio/AudioClipCache.h>
#include <core/assets/AssetRegistry.h>
#include <core/assets/AssetSystem.h>
#include <graphics/vulkan/TextureCache.h>
#include <render/MaterialCache.h>
#include <render/static_mesh/StaticMeshCache.h>

class LoggingProvider;
class VulkanBufferService;
class VulkanDescriptorCache;
class VulkanImageService;
class VulkanSamplerCache;

// Declaration order is load-bearing: Materials holds RAII references into
// Textures, so Textures must be declared first (destroyed last).
struct RuntimeAssets
{
    AssetRegistry Registry;
    TextureCache Textures;
    MaterialCache Materials;
    StaticMeshCache StaticMeshes;
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
        , StaticMeshes(logging, buffers)
        , AudioClips(logging)
        , Assets(logging, Registry, StaticMeshes, Materials, Textures, AudioClips)
    {
    }

    RuntimeAssets(const RuntimeAssets&) = delete;
    RuntimeAssets& operator=(const RuntimeAssets&) = delete;

    RuntimeAssets(RuntimeAssets&&) = delete;
    RuntimeAssets& operator=(RuntimeAssets&&) = delete;
};
