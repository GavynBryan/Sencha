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
// 16 KB sits comfortably within a typical L1 cache (32–64 KB) and is the
// well-known Unity DOTS sweet spot. At 64-byte cache lines that is 256 lines —
// enough to hold dozens of rows for a typical multi-component archetype.
// Named constant here; do not scatter magic numbers.
constexpr size_t ChunkSizeBytes = 16 * 1024;

// Per-column metadata stored in the chunk header.
struct ColumnDescriptor
{
    ComponentId Id;
    size_t      Offset;   // byte offset into Chunk::Data for column start
    size_t      Stride;   // sizeof(T) per element; 0 for tag components
    uint32_t    LastWrittenFrame; // conservative write-bump frame counter
};

// Chunk: a fixed-size slab of memory holding rows of one archetype.
// Columns are parallel arrays; one column per non-tag component in the signature.
// The entity index for each row is stored in a parallel EntityIndex array.
//
// Memory layout within Data[]:
//   [column 0: capacity * stride0 bytes]
//   [column 1: capacity * stride1 bytes]
//   ...
//   [entity indices: capacity * sizeof(EntityIndex) bytes]  <- last
//
// Alignment is natural (each column starts at an aligned offset).
// The layout is computed once at archetype creation; chunks are views into it.
struct Chunk
{
    // Raw storage — chunk data lives here.
    alignas(64) uint8_t Data[ChunkSizeBytes] = {};

    uint32_t RowCount    = 0;
    uint32_t RowCapacity = 0;

    // Column descriptors (owned by Archetype, pointed to here for hot-path access).
    // Non-owning: points into the Archetype's column-descriptor array.
    const ColumnDescriptor* Columns    = nullptr;
    uint32_t                ColumnCount = 0;

    // Byte offset of the entity-index column within Data[].
    size_t EntityColumnOffset = 0;

    bool IsFull()  const { return RowCount == RowCapacity; }
    bool IsEmpty() const { return RowCount == 0; }

    // --- Row access helpers ---

    EntityIndex* EntityIndices()
    {
        return reinterpret_cast<EntityIndex*>(Data + EntityColumnOffset);
    }

    const EntityIndex* EntityIndices() const
    {
        return reinterpret_cast<const EntityIndex*>(Data + EntityColumnOffset);
    }

    // Raw column pointer for column index col (not component id).
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

    // Typed span over a column — for use inside Query iteration.
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

    // Find column index for a given ComponentId. Returns UINT32_MAX if not found.
    uint32_t FindColumn(ComponentId id) const
    {
        for (uint32_t i = 0; i < ColumnCount; ++i)
            if (Columns[i].Id == id)
                return i;
        return UINT32_MAX;
    }

    // Bump the last-written-frame counter for a column by column index.
    void BumpColumnVersion(uint32_t col, uint32_t frame)
    {
        assert(col < ColumnCount);
        const_cast<ColumnDescriptor*>(Columns)[col].LastWrittenFrame = frame;
    }

    // Bump by ComponentId — used by Query after chunk callback.
    void BumpColumnVersionById(ComponentId id, uint32_t frame)
    {
        const uint32_t col = FindColumn(id);
        if (col != UINT32_MAX)
            const_cast<ColumnDescriptor*>(Columns)[col].LastWrittenFrame = frame;
    }
};
