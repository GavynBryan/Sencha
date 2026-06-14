#pragma once

#include <core/assets/AssetCache.h>
#include <core/handle/Owned.h>
#include <core/logging/LoggingProvider.h>
#include <render/static_mesh/GpuStaticMesh.h>
#include <render/static_mesh/MeshGeometry.h>
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
using StaticMeshCacheHandle = Owned<StaticMeshHandle>;

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

    [[nodiscard]] StaticMeshHandle Create(const MeshGeometry& data);
    [[nodiscard]] StaticMeshHandle CreateFromData(std::string_view name, const MeshGeometry& data);

    [[nodiscard]] StaticMeshHandle Acquire(std::string_view name);
    [[nodiscard]] StaticMeshCacheHandle AcquireOwned(std::string_view name);
    [[nodiscard]] StaticMeshHandle Find(std::string_view name) const;
    void Destroy(StaticMeshHandle handle);

    // Hot reload (Stage 6b): swaps the geometry of an existing resident entry
    // in place, keeping slot/generation/refcount/handle, and retires the old
    // GPU buffers through the deletion queue. Returns false if `path` has no
    // live entry — nothing to swap, and the next normal load picks up the new
    // bytes.
    [[nodiscard]] bool ReloadInPlace(std::string_view path, const MeshGeometry& data);

    [[nodiscard]] const GpuStaticMesh* Get(StaticMeshHandle handle) const;
    [[nodiscard]] std::string_view GetName(StaticMeshHandle handle) const;
    [[nodiscard]] bool IsAlive(StaticMeshHandle handle) const;

private:
    friend class AssetCache<StaticMeshCache, StaticMeshHandle, StaticMeshEntry>;

    bool OnLoad(std::string_view path, StaticMeshEntry& out);
    void OnFree(StaticMeshEntry& entry);
    bool IsEntryLive(const StaticMeshEntry& entry) const;

    bool UploadMesh(const MeshGeometry& data, StaticMeshEntry& out);

    Logger& Log;
    VulkanBufferService* Buffers = nullptr;
};
