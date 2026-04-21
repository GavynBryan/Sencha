#include <render/static_mesh/StaticMeshCache.h>

#include <graphics/vulkan/VulkanBufferService.h>
#include <render/static_mesh/StaticMeshValidation.h>

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

StaticMeshHandle StaticMeshCache::Create(const StaticMeshData& data)
{
    StaticMeshEntry entry;
    if (!UploadMesh(data, entry))
        return {};

    return AllocHandle(std::move(entry));
}

StaticMeshHandle StaticMeshCache::CreateFromData(std::string_view name, const StaticMeshData& data)
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

bool StaticMeshCache::IsEntryLive(const StaticMeshEntry& entry) const
{
    return entry.Alive;
}

bool StaticMeshCache::UploadMesh(const StaticMeshData& data, StaticMeshEntry& out)
{
    const StaticMeshValidationResult validation = ValidateStaticMeshData(data);
    if (!validation.IsValid())
    {
        for (const StaticMeshValidationError& error : validation.Errors)
            Log.Error("StaticMeshCache rejected mesh: {}", error.Message);
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

    out.Mesh = GpuStaticMesh{
        .VertexBuffer = vb,
        .IndexBuffer = ib,
        .VertexCount = static_cast<uint32_t>(data.Vertices.size()),
        .IndexCount = static_cast<uint32_t>(data.Indices.size()),
        .LocalBounds = data.LocalBounds,
        .Sections = data.Sections,
    };
    out.Alive = true;
    return true;
}
