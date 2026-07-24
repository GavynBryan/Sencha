#pragma once

#include <render/static_mesh/StaticMeshVertex.h>

#include <cstdint>

// Version 2: tangents joined the base vertex (Decision M; 32 -> 48 bytes).
// Taken once, alongside the glTF importer that emits them — there is no v1
// content to migrate: dev meshes regenerate at build time and cooked meshes
// recook.
// Version 3: the skinned-capable format (Decisions J, M, N). The reserved
// header fields became JointCount / SkinningDataOffset / SkeletonPathOffset,
// gated on the skinned flag bit; non-skinned files write them as zero and
// are otherwise byte-identical to v2. One version is live at a time — dev
// meshes regenerate, cooked meshes recook (the cooked-index bump).
// Version 4: the base vertex gained a 4-byte baked-static-direct RGBM channel
// (48 -> 52 bytes). Single version live: v3 files recooked (cooked-index bump).
// Version 5: baked lighting moved from vertices to per-zone lightmap atlases;
// the 4-byte slot at offset 48 became two unorm16 lightmap UVs
// (StaticMeshVertex::LightmapU/V; stride stays 52). Always present, zero when
// unbaked, so one vertex format and one draw path serve baked and unbaked
// meshes alike. The kSmeshFlagLightmapUv header bit records whether the cook
// wrote meaningful UVs (tooling hint only; the runtime always reads the
// channel). Single version live: v4 files must recook (cooked-index bump).
inline constexpr uint32_t kSmeshFormatVersion = 5;

// SmeshFileHeader::Flags bits.
inline constexpr uint32_t kSmeshFlagSkinned = 1u << 0;
inline constexpr uint32_t kSmeshFlagLightmapUv = 1u << 1;

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

    // Skinned meshes only (kSmeshFlagSkinned); all three are zero otherwise.
    // JointCount is the palette size either skinning runtime needs (Decision
    // N: knowable from the header alone). SkinningDataOffset locates
    // VertexCount MeshSkinInfluence records; SkeletonPathOffset
    // locates a u32 length followed by the skeleton's asset path bytes.
    uint32_t JointCount = 0;
    uint32_t SkinningDataOffset = 0;
    uint32_t SkeletonPathOffset = 0;

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
static_assert(sizeof(StaticMeshVertex) == 52);
