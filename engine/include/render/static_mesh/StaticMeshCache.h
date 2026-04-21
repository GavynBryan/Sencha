#pragma once

#include <core/assets/AssetCache.h>
#include <core/handle/LifetimeHandle.h>
#include <core/logging/LoggingProvider.h>
#include <render/static_mesh/GpuStaticMesh.h>
#include <render/static_mesh/StaticMeshData.h>
#include <render/static_mesh/StaticMeshHandle.h>

#include <cstdint>
#include <string>
#include <string_view>

class VulkanBufferService;

struct StaticMeshEntry
{
    GpuStaticMesh Mesh;
    uint32_t Generation = 0;
    uint32_t RefCount = 0;
    std::string PathKey;
    bool Alive = false;
};

class StaticMeshCache;
using StaticMeshCacheHandle = LifetimeHandle<StaticMeshCache, StaticMeshHandle>;

class StaticMeshCache final
    : public AssetCache<StaticMeshCache, StaticMeshHandle, StaticMeshEntry>
{
public:
    StaticMeshCache(LoggingProvider& logging, VulkanBufferService& buffers);
    ~StaticMeshCache() override;

    StaticMeshCache(const StaticMeshCache&) = delete;
    StaticMeshCache& operator=(const StaticMeshCache&) = delete;
    StaticMeshCache(StaticMeshCache&&) = delete;
    StaticMeshCache& operator=(StaticMeshCache&&) = delete;

    [[nodiscard]] StaticMeshHandle Create(const StaticMeshData& data);
    [[nodiscard]] StaticMeshHandle CreateFromData(std::string_view name, const StaticMeshData& data);

    [[nodiscard]] StaticMeshHandle Acquire(std::string_view name);
    [[nodiscard]] StaticMeshCacheHandle AcquireOwned(std::string_view name);
    [[nodiscard]] StaticMeshHandle Find(std::string_view name) const;
    void Destroy(StaticMeshHandle handle);

    [[nodiscard]] const GpuStaticMesh* Get(StaticMeshHandle handle) const;
    [[nodiscard]] std::string_view GetName(StaticMeshHandle handle) const;
    [[nodiscard]] bool IsAlive(StaticMeshHandle handle) const;

private:
    friend class AssetCache<StaticMeshCache, StaticMeshHandle, StaticMeshEntry>;

    bool OnLoad(std::string_view path, StaticMeshEntry& out);
    void OnFree(StaticMeshEntry& entry);
    bool IsEntryLive(const StaticMeshEntry& entry) const;

    bool UploadMesh(const StaticMeshData& data, StaticMeshEntry& out);

    Logger& Log;
    VulkanBufferService* Buffers = nullptr;
};
