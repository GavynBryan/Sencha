#pragma once

#include <anim/SkeletonHandle.h>
#include <core/assets/AssetCache.h>
#include <core/handle/Owned.h>
#include <core/logging/LoggingProvider.h>
#include <assets/skinned_mesh/SkinnedMeshData.h>
#include <render/skinned_mesh/SkinnedMeshHandle.h>
#include <graphics/BufferHandle.h>
#include <render/static_mesh/GpuStaticMesh.h>

#include <cstdint>
#include <string>
#include <string_view>


//=============================================================================
// SkinnedMeshCache (docs/assets/pipeline.md, Decisions J, M, N)
//
// The residency home for skinned meshes, distinct from StaticMeshCache
// because the runtime diverges: a skinned mesh holds a skeleton reference
// (the mesh→skeleton refcount chain), keeps its CPU skinning stream so the
// animation runtime can upload a bone-weighted buffer, and will later own
// per-instance posed-vertex buffers (Decision N). The rest geometry uploads
// through the same shared GPU path static meshes use.
//=============================================================================
struct SkinnedMeshEntry
{
    GpuStaticMesh Mesh;          // rest geometry on the GPU
    MeshSkinning Skinning;       // CPU influence stream + joint count (Decision N)
    // The influence stream on the GPU, parallel to the vertex buffer. Created
    // with both vertex-attribute and storage usage because Decision N has not
    // chosen between vertex-shader skinning and compute pre-skin -- the flags
    // are free, and either consumer binds this buffer as it is.
    BufferHandle Influences;
    SkeletonCacheHandle OwnedSkeleton; // releasing the mesh releases the skeleton

    uint32_t Generation = 0;
    uint32_t RefCount = 0;
    std::string PathKey;
    bool Alive = false;
};

class SkinnedMeshCache final
    : public AssetCache<SkinnedMeshCache, SkinnedMeshHandle, SkinnedMeshEntry, AssetType::SkinnedMesh>
{
public:
    SkinnedMeshCache(LoggingProvider& logging, GpuBuffers buffers);
    ~SkinnedMeshCache() override;

    SkinnedMeshCache(const SkinnedMeshCache&) = delete;
    SkinnedMeshCache& operator=(const SkinnedMeshCache&) = delete;
    SkinnedMeshCache(SkinnedMeshCache&&) = delete;
    SkinnedMeshCache& operator=(SkinnedMeshCache&&) = delete;

    // Uploads the rest geometry and registers the skinned mesh, taking
    // ownership of the skeleton reference its influences index. If `name`
    // already exists, the existing handle is returned and `ownedSkeleton`
    // releases immediately.
    [[nodiscard]] SkinnedMeshHandle CreateFromData(std::string_view name,
                                                   const SkinnedMeshData& data,
                                                   SkeletonCacheHandle ownedSkeleton);

    [[nodiscard]] SkinnedMeshHandle Acquire(std::string_view name);
    [[nodiscard]] SkinnedMeshCacheHandle AcquireOwned(std::string_view name);
    [[nodiscard]] SkinnedMeshHandle Find(std::string_view name) const;
    void Destroy(SkinnedMeshHandle handle);

    [[nodiscard]] const GpuStaticMesh* Get(SkinnedMeshHandle handle) const;
    [[nodiscard]] const MeshSkinning* GetSkinning(SkinnedMeshHandle handle) const;
    // The GPU-resident influence stream (joints u16x4 + weights u8x4 per
    // vertex), the pre-skin dispatch's second input. Null handle when the
    // mesh is not resident.
    [[nodiscard]] BufferHandle GetInfluences(SkinnedMeshHandle handle) const;
    // The skeleton this mesh is skinned to; resolve it through SkeletonCache
    // to reach the joint data a pose needs.
    [[nodiscard]] SkeletonHandle GetSkeletonHandle(SkinnedMeshHandle handle) const;
    [[nodiscard]] std::string_view GetName(SkinnedMeshHandle handle) const;
    [[nodiscard]] bool IsAlive(SkinnedMeshHandle handle) const;

private:
    friend class AssetCache<SkinnedMeshCache, SkinnedMeshHandle, SkinnedMeshEntry, AssetType::SkinnedMesh>;

    void OnFree(SkinnedMeshEntry& entry);
    bool IsEntryLive(const SkinnedMeshEntry& entry) const;

    Logger& Log;
    GpuBuffers Buffers;
};
