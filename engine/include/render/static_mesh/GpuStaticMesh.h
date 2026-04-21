#pragma once

#include <graphics/vulkan/VulkanBufferService.h>
#include <render/static_mesh/StaticMeshSection.h>

#include <vector>

struct GpuStaticMesh
{
    BufferHandle VertexBuffer;
    BufferHandle IndexBuffer;

    uint32_t VertexCount = 0;
    uint32_t IndexCount = 0;

    Aabb3d LocalBounds = Aabb3d::Empty();

    std::vector<StaticMeshSection> Sections;
};
