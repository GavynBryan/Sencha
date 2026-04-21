#pragma once

#include <core/assets/AssetRegistry.h>
#include <core/assets/AssetSystem.h>
#include <render/MaterialCache.h>
#include <render/static_mesh/StaticMeshCache.h>

class LoggingProvider;
class VulkanBufferService;

struct RuntimeAssets
{
    AssetRegistry Registry;
    MaterialCache Materials;
    StaticMeshCache StaticMeshes;
    AssetSystem Assets;

    RuntimeAssets(LoggingProvider& logging, VulkanBufferService& buffers)
        : Registry(logging)
        , Materials()
        , StaticMeshes(logging, buffers)
        , Assets(logging, Registry, StaticMeshes, Materials)
    {
    }

    RuntimeAssets(const RuntimeAssets&) = delete;
    RuntimeAssets& operator=(const RuntimeAssets&) = delete;

    RuntimeAssets(RuntimeAssets&&) = delete;
    RuntimeAssets& operator=(RuntimeAssets&&) = delete;
};
