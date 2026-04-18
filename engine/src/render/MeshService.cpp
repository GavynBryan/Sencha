#include <render/MeshService.h>

#include <graphics/vulkan/VulkanBufferService.h>

MeshService::MeshService(LoggingProvider& logging, VulkanBufferService& buffers)
    : Log(logging.GetLogger<MeshService>())
    , Buffers(&buffers)
{
}

MeshService::~MeshService()
{
    for (auto& entry : Entries)
    {
        if (!entry.Alive) continue;
        Buffers->Destroy(entry.Mesh.VertexBuffer);
        Buffers->Destroy(entry.Mesh.IndexBuffer);
    }
}

MeshHandle MeshService::Create(const MeshData& data)
{
    if (data.Vertices.empty() || data.Indices.empty())
    {
        Log.Error("MeshService::Create requires vertices and indices");
        return {};
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
        return {};
    }

    if (!Buffers->Upload(vb, data.Vertices.data(), vbInfo.Size)
        || !Buffers->Upload(ib, data.Indices.data(), ibInfo.Size))
    {
        Buffers->Destroy(vb);
        Buffers->Destroy(ib);
        return {};
    }

    uint32_t index = 0;
    if (!FreeSlots.empty())
    {
        index = FreeSlots.back();
        FreeSlots.pop_back();
    }
    else
    {
        index = static_cast<uint32_t>(Entries.size());
        Entries.emplace_back();
    }

    Entry& entry = Entries[index];
    entry.Mesh = GpuMesh{
        .VertexBuffer = vb,
        .IndexBuffer = ib,
        .VertexCount = static_cast<uint32_t>(data.Vertices.size()),
        .IndexCount = static_cast<uint32_t>(data.Indices.size()),
        .LocalBounds = data.LocalBounds,
        .Submeshes = data.Submeshes,
    };
    entry.Alive = true;
    ++entry.Generation;
    if (entry.Generation == 0) entry.Generation = 1;  // skip 0; IsValid() treats it as null

    return { index, entry.Generation };
}

void MeshService::Destroy(MeshHandle handle)
{
    if (!handle.IsValid() || handle.Index >= Entries.size()) return;
    Entry& entry = Entries[handle.Index];
    if (!entry.Alive || entry.Generation != handle.Generation) return;

    Buffers->Destroy(entry.Mesh.VertexBuffer);
    Buffers->Destroy(entry.Mesh.IndexBuffer);
    entry.Mesh = {};
    entry.Alive = false;
    FreeSlots.push_back(handle.Index);
}

const GpuMesh* MeshService::Get(MeshHandle handle) const
{
    if (!handle.IsValid() || handle.Index >= Entries.size()) return nullptr;
    const Entry& entry = Entries[handle.Index];
    return entry.Alive && entry.Generation == handle.Generation ? &entry.Mesh : nullptr;
}
