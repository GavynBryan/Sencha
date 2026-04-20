#pragma once

#include <core/assets/AssetRegistry.h>
#include <core/assets/AssetSystem.h>
#include <render/MaterialCache.h>
#include <render/MeshCache.h>

class LoggingProvider;
class VulkanBufferService;

struct RuntimeAssets
{
    AssetRegistry Registry;
    MaterialCache Materials;
    MeshCache Meshes;
    AssetSystem Assets;

    RuntimeAssets(LoggingProvider& logging, VulkanBufferService& buffers)
        : Registry(logging)
        , Materials()
        , Meshes(logging, buffers)
        , Assets(Registry, Meshes, Materials)
    {
    }

    RuntimeAssets(const RuntimeAssets&) = delete;
    RuntimeAssets& operator=(const RuntimeAssets&) = delete;

    RuntimeAssets(RuntimeAssets&&) = delete;
    RuntimeAssets& operator=(RuntimeAssets&&) = delete;
};
