#include <render/MeshCache.h>

#include <graphics/vulkan/VulkanBufferService.h>

MeshCache::MeshCache(LoggingProvider& logging, VulkanBufferService& buffers)
    : Log(logging.GetLogger<MeshCache>())
    , Buffers(&buffers)
{
    ReserveNullSlot();
}

MeshCache::~MeshCache()
{
    FreeAllEntries();
}

MeshHandle MeshCache::Create(const MeshData& data)
{
    MeshEntry entry;
    if (!UploadMesh(data, entry))
        return {};

    return AllocHandle(std::move(entry));
}

MeshHandle MeshCache::CreateFromData(std::string_view name, const MeshData& data)
{
    if (name.empty())
        return Create(data);

    if (MeshHandle existing = FindRegisteredHandle(name); existing.IsValid())
        return existing;

    MeshEntry entry;
    if (!UploadMesh(data, entry))
        return {};

    return AllocNamedHandle(name, std::move(entry));
}

MeshHandle MeshCache::Acquire(std::string_view name)
{
    return FindRegisteredHandle(name, true);
}

MeshCacheHandle MeshCache::AcquireOwned(std::string_view name)
{
    MeshHandle handle = Acquire(name);
    if (!handle.IsValid())
        return {};

    return MeshCacheHandle(this, handle, MeshCacheHandle::NoAttach);
}

MeshHandle MeshCache::Find(std::string_view name) const
{
    return FindRegisteredHandle(name);
}

void MeshCache::Destroy(MeshHandle handle)
{
    Release(handle);
}

const GpuMesh* MeshCache::Get(MeshHandle handle) const
{
    const MeshEntry* entry = Resolve(handle);
    return entry ? &entry->Mesh : nullptr;
}

std::string_view MeshCache::GetName(MeshHandle handle) const
{
    return GetRegisteredPath(handle);
}

bool MeshCache::OnLoad(std::string_view, MeshEntry&)
{
    return false;
}

void MeshCache::OnFree(MeshEntry& entry)
{
    if (entry.Mesh.VertexBuffer.IsValid())
    {
        Buffers->Destroy(entry.Mesh.VertexBuffer);
        entry.Mesh.VertexBuffer = {};
    }

    if (entry.Mesh.IndexBuffer.IsValid())
    {
        Buffers->Destroy(entry.Mesh.IndexBuffer);
        entry.Mesh.IndexBuffer = {};
    }

    entry.Mesh = {};
    entry.Alive = false;
}

bool MeshCache::IsEntryLive(const MeshEntry& entry) const
{
    return entry.Alive;
}

bool MeshCache::UploadMesh(const MeshData& data, MeshEntry& out)
{
    if (data.Vertices.empty() || data.Indices.empty())
    {
        Log.Error("MeshCache::Create requires vertices and indices");
        return false;
    }

    BufferCreateInfo vbInfo{};
    vbInfo.Size = sizeof(StaticMeshVertex) * data.Vertices.size();
    vbInfo.Usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    vbInfo.Memory = BufferMemory::GpuOnly;
    vbInfo.DebugName = "Static mesh vertex buffer";

    BufferCreateInfo ibInfo{};
    ibInfo.Size = sizeof(uint32_t) * data.Indices.size();
    ibInfo.Usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    ibInfo.Memory = BufferMemory::GpuOnly;
    ibInfo.DebugName = "Static mesh index buffer";

    BufferHandle vb = Buffers->Create(vbInfo);
    BufferHandle ib = Buffers->Create(ibInfo);
    if (!vb.IsValid() || !ib.IsValid())
    {
        Buffers->Destroy(vb);
        Buffers->Destroy(ib);
        return false;
    }

    if (!Buffers->Upload(vb, data.Vertices.data(), vbInfo.Size)
        || !Buffers->Upload(ib, data.Indices.data(), ibInfo.Size))
    {
        Buffers->Destroy(vb);
        Buffers->Destroy(ib);
        return false;
    }

    out.Mesh = GpuMesh{
        .VertexBuffer = vb,
        .IndexBuffer = ib,
        .VertexCount = static_cast<uint32_t>(data.Vertices.size()),
        .IndexCount = static_cast<uint32_t>(data.Indices.size()),
        .LocalBounds = data.LocalBounds,
        .Submeshes = data.Submeshes,
    };
    out.Alive = true;
    return true;
}
