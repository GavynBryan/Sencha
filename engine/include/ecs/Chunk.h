#pragma once

#include <ecs/ComponentId.h>
#include <ecs/EntityId.h>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

// Chunk size in bytes.
// 16 KB sits within a typical L1 cache (32–64 KB) and is the well-known Unity DOTS
// sweet spot: 256 cache lines × 64 bytes. Gives ~585 rows for a {Position, Velocity}
// archetype while staying L1-resident during a sweep.
// See docs/ecs/decisions.md D0.1 for the full rationale.
constexpr size_t ChunkSizeBytes = 16 * 1024;

// Per-column metadata stored in the chunk header.
struct ColumnDescriptor
{
    ComponentId Id;
    size_t      Offset;           // byte offset into Chunk::Data for column start
    size_t      Stride;           // sizeof(T) per element; 0 for tag components
};

// Chunk: a fixed-size slab of memory holding rows of one archetype.
// Columns are parallel arrays; one column per non-tag component in the signature.
//
// Memory layout within Data[]:
//   [column 0: capacity * stride0 bytes]
//   [column 1: capacity * stride1 bytes]
//   ...
//   [entity indices: capacity * sizeof(EntityIndex) bytes]  <- last
//
// See docs/ecs/decisions.md D0.3 for column-first vs AoS rationale.
struct Chunk
{
    alignas(64) uint8_t Data[ChunkSizeBytes] = {};

    uint32_t RowCount    = 0;
    uint32_t RowCapacity = 0;

    // Non-owning: points into the owning Archetype's column-descriptor vector.
    const ColumnDescriptor* Columns     = nullptr;
    uint32_t                ColumnCount = 0;
    std::vector<uint32_t>   LastWrittenFrames;

    size_t EntityColumnOffset = 0;

    bool IsFull()  const { return RowCount == RowCapacity; }
    bool IsEmpty() const { return RowCount == 0; }

    EntityIndex* EntityIndices()
    {
        return reinterpret_cast<EntityIndex*>(Data + EntityColumnOffset);
    }

    const EntityIndex* EntityIndices() const
    {
        return reinterpret_cast<const EntityIndex*>(Data + EntityColumnOffset);
    }

    uint8_t* ColumnData(uint32_t col)
    {
        assert(col < ColumnCount);
        return Data + Columns[col].Offset;
    }

    const uint8_t* ColumnData(uint32_t col) const
    {
        assert(col < ColumnCount);
        return Data + Columns[col].Offset;
    }

    template <typename T>
    std::span<T> ColumnSpan(uint32_t col)
    {
        assert(col < ColumnCount);
        assert(Columns[col].Stride == sizeof(T));
        return { reinterpret_cast<T*>(Data + Columns[col].Offset), RowCount };
    }

    template <typename T>
    std::span<const T> ColumnSpan(uint32_t col) const
    {
        assert(col < ColumnCount);
        assert(Columns[col].Stride == sizeof(T));
        return { reinterpret_cast<const T*>(Data + Columns[col].Offset), RowCount };
    }

    // Returns UINT32_MAX if not found.
    uint32_t FindColumn(ComponentId id) const
    {
        for (uint32_t i = 0; i < ColumnCount; ++i)
            if (Columns[i].Id == id)
                return i;
        return UINT32_MAX;
    }

    void BumpColumnVersion(uint32_t col, uint32_t frame)
    {
        assert(col < ColumnCount);
        assert(col < LastWrittenFrames.size());
        LastWrittenFrames[col] = frame;
    }

    uint32_t ColumnLastWrittenFrame(uint32_t col) const
    {
        assert(col < LastWrittenFrames.size());
        return LastWrittenFrames[col];
    }

    void BumpColumnVersionById(ComponentId id, uint32_t frame)
    {
        const uint32_t col = FindColumn(id);
        if (col != UINT32_MAX)
            BumpColumnVersion(col, frame);
    }
};
