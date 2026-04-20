#pragma once

#include <core/assets/AssetCache.h>
#include <core/handle/LifetimeHandle.h>
#include <core/logging/LoggingProvider.h>
#include <render/MeshTypes.h>

#include <cstdint>
#include <string>
#include <string_view>

class VulkanBufferService;

struct MeshEntry
{
    GpuMesh Mesh;
    uint32_t Generation = 0;
    uint32_t RefCount = 0;
    std::string PathKey;
    bool Alive = false;
};

class MeshCache;
using MeshCacheHandle = LifetimeHandle<MeshCache, MeshHandle>;

//=============================================================================
// MeshCache
//
// Generational cache for GPU meshes. Named meshes can be resolved by stable
// string keys for scene serialization; unnamed meshes support one-off runtime
// procedural data.
//=============================================================================
class MeshCache final : public AssetCache<MeshCache, MeshHandle, MeshEntry>
{
public:
    MeshCache(LoggingProvider& logging, VulkanBufferService& buffers);
    ~MeshCache() override;

    MeshCache(const MeshCache&) = delete;
    MeshCache& operator=(const MeshCache&) = delete;
    MeshCache(MeshCache&&) = delete;
    MeshCache& operator=(MeshCache&&) = delete;

    [[nodiscard]] MeshHandle Create(const MeshData& data);
    [[nodiscard]] MeshHandle CreateFromData(std::string_view name, const MeshData& data);

    [[nodiscard]] MeshHandle Acquire(std::string_view name);
    [[nodiscard]] MeshCacheHandle AcquireOwned(std::string_view name);
    [[nodiscard]] MeshHandle Find(std::string_view name) const;
    void Destroy(MeshHandle handle);

    [[nodiscard]] const GpuMesh* Get(MeshHandle handle) const;
    [[nodiscard]] std::string_view GetName(MeshHandle handle) const;

private:
    friend class AssetCache<MeshCache, MeshHandle, MeshEntry>;

    bool OnLoad(std::string_view path, MeshEntry& out);
    void OnFree(MeshEntry& entry);
    bool IsEntryLive(const MeshEntry& entry) const;

    bool UploadMesh(const MeshData& data, MeshEntry& out);

    Logger& Log;
    VulkanBufferService* Buffers = nullptr;
};
