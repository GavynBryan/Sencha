#include <render/static_mesh/GpuStaticMesh.h>

#include <core/logging/Logger.h>
#include <render/static_mesh/MeshValidation.h>

bool UploadMeshGeometryToGpu(VulkanBufferService& buffers,
                             const MeshGeometry& geometry,
                             GpuStaticMesh& out,
                             Logger& log)
{
    const MeshValidationResult validation = ValidateMeshGeometry(geometry);
    if (!validation.IsValid())
    {
        for (const MeshValidationError& error : validation.Errors)
            log.Error("UploadMeshGeometryToGpu rejected mesh: {}", error.Message);
        return false;
    }

    BufferCreateInfo vbInfo{};
    vbInfo.Size = sizeof(StaticMeshVertex) * geometry.Vertices.size();
    vbInfo.Usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    vbInfo.Memory = BufferMemory::GpuOnly;
    vbInfo.DebugName = "Mesh vertex buffer";

    BufferCreateInfo ibInfo{};
    ibInfo.Size = sizeof(uint32_t) * geometry.Indices.size();
    ibInfo.Usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    ibInfo.Memory = BufferMemory::GpuOnly;
    ibInfo.DebugName = "Mesh index buffer";

    BufferHandle vb = buffers.Create(vbInfo);
    BufferHandle ib = buffers.Create(ibInfo);
    if (!vb.IsValid() || !ib.IsValid())
    {
        buffers.Destroy(vb);
        buffers.Destroy(ib);
        return false;
    }

    if (!buffers.Upload(vb, geometry.Vertices.data(), vbInfo.Size)
        || !buffers.Upload(ib, geometry.Indices.data(), ibInfo.Size))
    {
        buffers.Destroy(vb);
        buffers.Destroy(ib);
        return false;
    }

    out = GpuStaticMesh{
        .VertexBuffer = vb,
        .IndexBuffer = ib,
        .VertexCount = static_cast<uint32_t>(geometry.Vertices.size()),
        .IndexCount = static_cast<uint32_t>(geometry.Indices.size()),
        .LocalBounds = geometry.LocalBounds,
        .Sections = geometry.Sections,
    };
    return true;
}

void DestroyGpuMesh(VulkanBufferService& buffers, GpuStaticMesh& mesh)
{
    if (mesh.VertexBuffer.IsValid())
        buffers.Destroy(mesh.VertexBuffer);
    if (mesh.IndexBuffer.IsValid())
        buffers.Destroy(mesh.IndexBuffer);
    mesh = {};
}
