#pragma once

#include <render/static_mesh/StaticMeshVertex.h>

#include <cstdint>

enum class SmeshIndexFormat : uint32_t
{
    UInt32 = 0,
};

enum class SmeshTopology : uint32_t
{
    TriangleList = 0,
};

struct SmeshFileHeader
{
    char Magic[4];
    uint32_t Version = 0;

    uint32_t Flags = 0;
    uint32_t Reserved0 = 0;
    uint32_t Reserved1 = 0;
    uint32_t Reserved2 = 0;

    uint32_t VertexCount = 0;
    uint32_t IndexCount = 0;
    uint32_t SectionCount = 0;

    uint32_t VertexStride = 0;
    SmeshIndexFormat IndexFormat = SmeshIndexFormat::UInt32;
    SmeshTopology Topology = SmeshTopology::TriangleList;

    float BoundsMin[3]{};
    float BoundsMax[3]{};

    uint32_t HeaderSize = 0;
    uint32_t SectionTableOffset = 0;
    uint32_t VertexDataOffset = 0;
    uint32_t IndexDataOffset = 0;
};

struct SmeshSectionRecord
{
    uint32_t IndexOffset = 0;
    uint32_t IndexCount = 0;

    uint32_t VertexOffset = 0;
    uint32_t VertexCount = 0;

    uint32_t MaterialSlot = 0;
    uint32_t Reserved0 = 0;

    float BoundsMin[3]{};
    float BoundsMax[3]{};
};

static_assert(sizeof(SmeshFileHeader) == 88);
static_assert(sizeof(SmeshSectionRecord) == 48);
static_assert(sizeof(StaticMeshVertex) == 32);
