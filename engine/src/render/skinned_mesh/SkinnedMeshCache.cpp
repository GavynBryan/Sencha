#include <render/skinned_mesh/SkinnedMeshCache.h>


#include <span>
#include <utility>

SkinnedMeshCache::SkinnedMeshCache(LoggingProvider& logging, GpuBuffers buffers)
    : Log(logging.GetLogger<SkinnedMeshCache>())
    , Buffers(buffers)
{
    ReserveNullSlot();
}

SkinnedMeshCache::~SkinnedMeshCache()
{
    FreeAllEntries();
}

SkinnedMeshHandle SkinnedMeshCache::CreateFromData(std::string_view name,
                                                   const SkinnedMeshData& data,
                                                   SkeletonCacheHandle ownedSkeleton)
{
    // Existing entry already owns its own skeleton reference; ownedSkeleton
    // releases on return.
    if (!name.empty())
        if (SkinnedMeshHandle existing = FindRegisteredHandle(name); existing.IsValid())
            return existing;

    SkinnedMeshEntry entry;
    // Rest geometry is the pre-skin dispatch's input as well as a fallback
    // vertex stream, so it is created readable both ways.
    if (!UploadMeshGeometryToGpu(Buffers, data.Geometry, entry.Mesh, Log,
                                 MeshVertexAccess::VertexAndCompute))
        return {};

    if (!data.Skinning.Influences.empty())
    {
        entry.Influences = UploadVertexSideStreamToGpu(
            Buffers, std::as_bytes(std::span(data.Skinning.Influences)),
            "Skinned mesh influences", Log);
        if (!entry.Influences.IsValid())
        {
            Log.Error("SkinnedMeshCache: influence upload failed for '{}'", name);
            DestroyGpuMesh(Buffers, entry.Mesh);
            return {};
        }
    }
    entry.Skinning = data.Skinning;
    entry.OwnedSkeleton = std::move(ownedSkeleton);
    entry.Alive = true;

    return name.empty() ? AllocHandle(std::move(entry)) : AllocNamedHandle(name, std::move(entry));
}

SkinnedMeshHandle SkinnedMeshCache::Acquire(std::string_view name)
{
    return FindRegisteredHandle(name, true);
}

SkinnedMeshCacheHandle SkinnedMeshCache::AcquireOwned(std::string_view name)
{
    SkinnedMeshHandle handle = Acquire(name);
    if (!handle.IsValid())
        return {};
    return SkinnedMeshCacheHandle(this, handle, SkinnedMeshCacheHandle::NoAttach);
}

SkinnedMeshHandle SkinnedMeshCache::Find(std::string_view name) const
{
    return FindRegisteredHandle(name);
}

void SkinnedMeshCache::Destroy(SkinnedMeshHandle handle)
{
    Release(handle);
}

const GpuStaticMesh* SkinnedMeshCache::Get(SkinnedMeshHandle handle) const
{
    const SkinnedMeshEntry* entry = Resolve(handle);
    return entry ? &entry->Mesh : nullptr;
}

const MeshSkinning* SkinnedMeshCache::GetSkinning(SkinnedMeshHandle handle) const
{
    const SkinnedMeshEntry* entry = Resolve(handle);
    return entry ? &entry->Skinning : nullptr;
}

BufferHandle SkinnedMeshCache::GetInfluences(SkinnedMeshHandle handle) const
{
    const SkinnedMeshEntry* entry = Resolve(handle);
    return entry ? entry->Influences : BufferHandle{};
}

SkeletonHandle SkinnedMeshCache::GetSkeletonHandle(SkinnedMeshHandle handle) const
{
    const SkinnedMeshEntry* entry = Resolve(handle);
    return entry ? entry->OwnedSkeleton.GetToken() : SkeletonHandle{};
}

std::string_view SkinnedMeshCache::GetName(SkinnedMeshHandle handle) const
{
    return GetRegisteredPath(handle);
}

bool SkinnedMeshCache::IsAlive(SkinnedMeshHandle handle) const
{
    const SkinnedMeshEntry* entry = Resolve(handle);
    return entry != nullptr && entry->Alive;
}

void SkinnedMeshCache::OnFree(SkinnedMeshEntry& entry)
{
    DestroyGpuMesh(Buffers, entry.Mesh);
    Buffers.Destroy(entry.Influences);
    entry.Influences = {};
    entry.Skinning = {};
    entry.OwnedSkeleton.Reset(); // releases the skeleton reference
    entry.Alive = false;
}

bool SkinnedMeshCache::IsEntryLive(const SkinnedMeshEntry& entry) const
{
    return entry.Alive;
}
