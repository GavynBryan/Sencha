#include <render/static_mesh/StaticMeshCache.h>

#include <graphics/vulkan/VulkanBufferService.h>
#include <render/static_mesh/MeshValidation.h>

StaticMeshCache::StaticMeshCache(LoggingProvider& logging, VulkanBufferService& buffers)
    : Log(logging.GetLogger<StaticMeshCache>())
    , Buffers(&buffers)
{
    ReserveNullSlot();
}

StaticMeshCache::~StaticMeshCache()
{
    FreeAllEntries();
}

StaticMeshHandle StaticMeshCache::Create(const MeshGeometry& data)
{
    StaticMeshEntry entry;
    if (!UploadMesh(data, entry))
        return {};

    return AllocHandle(std::move(entry));
}

StaticMeshHandle StaticMeshCache::CreateFromData(std::string_view name, const MeshGeometry& data)
{
    if (name.empty())
        return Create(data);

    if (StaticMeshHandle existing = FindRegisteredHandle(name); existing.IsValid())
        return existing;

    StaticMeshEntry entry;
    if (!UploadMesh(data, entry))
        return {};

    return AllocNamedHandle(name, std::move(entry));
}

StaticMeshHandle StaticMeshCache::Acquire(std::string_view name)
{
    return FindRegisteredHandle(name, true);
}

StaticMeshCacheHandle StaticMeshCache::AcquireOwned(std::string_view name)
{
    StaticMeshHandle handle = Acquire(name);
    if (!handle.IsValid())
        return {};

    return StaticMeshCacheHandle(this, handle, StaticMeshCacheHandle::NoAttach);
}

StaticMeshHandle StaticMeshCache::Find(std::string_view name) const
{
    return FindRegisteredHandle(name);
}

void StaticMeshCache::Destroy(StaticMeshHandle handle)
{
    Release(handle);
}

const GpuStaticMesh* StaticMeshCache::Get(StaticMeshHandle handle) const
{
    const StaticMeshEntry* entry = Resolve(handle);
    return entry ? &entry->Mesh : nullptr;
}

std::string_view StaticMeshCache::GetName(StaticMeshHandle handle) const
{
    return GetRegisteredPath(handle);
}

bool StaticMeshCache::IsAlive(StaticMeshHandle handle) const
{
    const StaticMeshEntry* entry = Resolve(handle);
    return entry != nullptr && entry->Alive;
}

bool StaticMeshCache::OnLoad(std::string_view, StaticMeshEntry&)
{
    return false;
}

void StaticMeshCache::OnFree(StaticMeshEntry& entry)
{
    DestroyGpuMesh(*Buffers, entry.Mesh);
    entry.Alive = false;
}

bool StaticMeshCache::IsEntryLive(const StaticMeshEntry& entry) const
{
    return entry.Alive;
}

bool StaticMeshCache::UploadMesh(const MeshGeometry& data, StaticMeshEntry& out)
{
    if (!UploadMeshGeometryToGpu(*Buffers, data, out.Mesh, Log))
        return false;
    out.Alive = true;
    return true;
}
