#include <render/static_mesh/GpuStaticMesh.h>

#include <core/logging/Logger.h>
#include <assets/static_mesh/MeshValidation.h>

bool UploadMeshGeometryToGpu(GpuBuffers buffers,
                             const MeshGeometry& geometry,
                             GpuStaticMesh& out,
                             Logger& log,
                             MeshVertexAccess access)
{
    const MeshValidationResult validation = ValidateMeshGeometry(geometry);
    if (!validation.IsValid())
    {
        for (const MeshValidationError& error : validation.Errors)
            log.Error("UploadMeshGeometryToGpu rejected mesh: {}", error.Message);
        return false;
    }

    BufferDesc vbInfo{};
    vbInfo.Size = sizeof(StaticMeshVertex) * geometry.Vertices.size();
    vbInfo.Usage = GpuBufferUsage::Vertex;
    if (access == MeshVertexAccess::VertexAndCompute)
        vbInfo.Usage = vbInfo.Usage | GpuBufferUsage::Storage;
    vbInfo.Memory = BufferMemory::GpuOnly;
    vbInfo.DebugName = "Mesh vertex buffer";

    BufferDesc ibInfo{};
    ibInfo.Size = sizeof(uint32_t) * geometry.Indices.size();
    ibInfo.Usage = GpuBufferUsage::Index;
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
        .HasLightmapUvs = GeometryHasLightmapUvs(geometry),
        .Sections = geometry.Sections,
    };
    return true;
}

BufferHandle UploadVertexSideStreamToGpu(GpuBuffers buffers,
                                         std::span<const std::byte> bytes,
                                         const char* debugName,
                                         Logger& log)
{
    if (bytes.empty())
        return {};

    BufferDesc info{};
    info.Size = bytes.size();
    info.Usage = GpuBufferUsage::Vertex | GpuBufferUsage::Storage;
    info.Memory = BufferMemory::GpuOnly;
    info.DebugName = debugName;

    const BufferHandle handle = buffers.Create(info);
    if (!handle.IsValid())
    {
        log.Error("UploadVertexSideStreamToGpu: buffer creation failed ({})", debugName);
        return {};
    }
    if (!buffers.Upload(handle, bytes.data(), info.Size))
    {
        log.Error("UploadVertexSideStreamToGpu: upload failed ({})", debugName);
        buffers.Destroy(handle);
        return {};
    }
    return handle;
}

void DestroyGpuMesh(GpuBuffers buffers, GpuStaticMesh& mesh)
{
    if (mesh.VertexBuffer.IsValid())
        buffers.Destroy(mesh.VertexBuffer);
    if (mesh.IndexBuffer.IsValid())
        buffers.Destroy(mesh.IndexBuffer);
    mesh = {};
}
