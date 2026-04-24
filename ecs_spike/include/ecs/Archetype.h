#pragma once

#include <ecs/ArchetypeSignature.h>
#include <ecs/Chunk.h>
#include <ecs/ComponentId.h>
#include <ecs/EntityId.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

// Per-component registration info needed to build an archetype's column layout.
struct ComponentInfo
{
    ComponentId Id;
    size_t      Size;      // sizeof(T); 0 for tag components
    size_t      Alignment; // alignof(T); 1 for tag components
};

// Archetype: the metadata for a unique component signature.
// Owns column descriptors and the list of live chunks.
// Entity location within an archetype is (chunkIndex, rowIndex).
struct Archetype
{
    ArchetypeSignature Signature;

    // Column descriptors — one per data component (tags excluded).
    // Owned here; Chunk::Columns points into this vector.
    // Do not reallocate after chunks are created.
    std::vector<ColumnDescriptor> Columns;

    // Chunks owned by this archetype.
    std::vector<std::unique_ptr<Chunk>> Chunks;

    // Rows per chunk, computed from chunk size and column layout.
    uint32_t RowsPerChunk = 0;

    // Archetype id (index into World::Archetypes).
    uint32_t Id = 0;

    // Build column layout from a list of ComponentInfos (tag components filtered out).
    // Must be called once before creating any chunks.
    void BuildLayout(const std::vector<ComponentInfo>& components)
    {
        Columns.clear();

        // Determine how many rows fit in ChunkSizeBytes.
        // Layout: [col0 * N][col1 * N]...[EntityIndex * N], aligned.
        // We solve: sum(stride_i * N) + sizeof(EntityIndex) * N <= ChunkSizeBytes
        // where stride_i accounts for element alignment within the column.

        // First pass: compute row byte size (sum of aligned strides).
        size_t rowByteSize = sizeof(EntityIndex); // entity index column
        for (const auto& comp : components)
        {
            if (comp.Size == 0) continue; // tag component — no column
            rowByteSize += comp.Size;
        }

        if (rowByteSize == 0)
        {
            // All-tag archetype: no data columns, capacity is arbitrary (use 64).
            RowsPerChunk = 64;
        }
        else
        {
            RowsPerChunk = static_cast<uint32_t>(ChunkSizeBytes / rowByteSize);
            if (RowsPerChunk == 0)
                RowsPerChunk = 1; // component larger than chunk; always fit one
        }

        // Second pass: assign column offsets at capacity * stride boundaries.
        size_t offset = 0;
        for (const auto& comp : components)
        {
            if (comp.Size == 0) continue;

            // Align offset to component's alignment.
            offset = (offset + comp.Alignment - 1) & ~(comp.Alignment - 1);

            ColumnDescriptor desc{};
            desc.Id               = comp.Id;
            desc.Offset           = offset;
            desc.Stride           = comp.Size;
            desc.LastWrittenFrame = 0;
            Columns.push_back(desc);

            offset += comp.Size * RowsPerChunk;
        }

        // Entity index column comes last.
        offset = (offset + alignof(EntityIndex) - 1) & ~(alignof(EntityIndex) - 1);
        // Store entity column offset in a temp; chunks will pick it up.
        // We remember it as EntityColumnOffset_ and apply during AllocChunk.
        EntityColumnOffset_ = offset;

        // Verify layout fits.
        assert(offset + sizeof(EntityIndex) * RowsPerChunk <= ChunkSizeBytes
               && "Archetype column layout exceeds chunk size");
    }

    // Allocate a new chunk. Returns a raw pointer (owned by Chunks).
    Chunk* AllocChunk()
    {
        auto chunk = std::make_unique<Chunk>();
        chunk->RowCount         = 0;
        chunk->RowCapacity      = RowsPerChunk;
        chunk->Columns          = Columns.data();
        chunk->ColumnCount      = static_cast<uint32_t>(Columns.size());
        chunk->EntityColumnOffset = EntityColumnOffset_;

        Chunks.push_back(std::move(chunk));
        return Chunks.back().get();
    }

    // Get or create a chunk with space for at least one row.
    // Prefers the last chunk; allocates a new one if full.
    // (Interior empty chunks are not compacted in the spike — see RemoveRow note.)
    Chunk* GetOrAllocChunkWithSpace()
    {
        if (!Chunks.empty() && !Chunks.back()->IsFull())
            return Chunks.back().get();
        return AllocChunk();
    }

    // Add a row to the archetype for the given entity.
    // Returns (chunkIndex, rowIndex) of the new row.
    // Component data must be written by the caller after this returns.
    std::pair<uint32_t, uint32_t> AddRow(EntityIndex entityIndex)
    {
        Chunk* chunk = GetOrAllocChunkWithSpace();
        const uint32_t chunkIdx = static_cast<uint32_t>(Chunks.size()) - 1;
        const uint32_t rowIdx   = chunk->RowCount++;
        chunk->EntityIndices()[rowIdx] = entityIndex;
        return { chunkIdx, rowIdx };
    }

    // Remove a row by swapping with the last row in the same chunk.
    // Returns the EntityIndex of the entity that was moved to fill the gap
    // (InvalidEntityIndex if no swap was needed, i.e., the row was already last).
    EntityIndex RemoveRow(uint32_t chunkIdx, uint32_t rowIdx)
    {
        assert(chunkIdx < Chunks.size());
        Chunk* chunk = Chunks[chunkIdx].get();
        assert(rowIdx < chunk->RowCount);

        const uint32_t lastRow = chunk->RowCount - 1;
        EntityIndex movedEntity = InvalidEntityIndex;

        if (rowIdx != lastRow)
        {
            // Swap last row into the gap.
            movedEntity = chunk->EntityIndices()[lastRow];
            chunk->EntityIndices()[rowIdx] = movedEntity;

            // Copy component data columns.
            for (uint32_t c = 0; c < chunk->ColumnCount; ++c)
            {
                const size_t stride = chunk->Columns[c].Stride;
                if (stride == 0) continue;
                uint8_t* dst = chunk->ColumnData(c) + rowIdx  * stride;
                uint8_t* src = chunk->ColumnData(c) + lastRow * stride;
                std::memcpy(dst, src, stride);
            }
        }

        --chunk->RowCount;

        // Do not drop empty chunks — doing so would require updating EntityLocations
        // for all entities in the relocated chunk, which requires registry access
        // that Archetype does not have. The slot reuse in GetOrAllocChunkWithSpace
        // prefers the last chunk; empty interior chunks are recycled when they
        // become the last one naturally. This is acceptable for the spike;
        // Phase 1 will implement compaction with proper location fixup.

        return movedEntity;
    }

    // Write a single component's data into a row.
    // T must match the stride of the column.
    template <typename T>
    void WriteComponent(uint32_t chunkIdx, uint32_t rowIdx, ComponentId id, const T& value)
    {
        assert(chunkIdx < Chunks.size());
        Chunk* chunk = Chunks[chunkIdx].get();
        const uint32_t col = chunk->FindColumn(id);
        assert(col != UINT32_MAX && "Component not found in archetype");
        assert(chunk->Columns[col].Stride == sizeof(T));
        T* ptr = reinterpret_cast<T*>(chunk->ColumnData(col)) + rowIdx;
        std::memcpy(ptr, &value, sizeof(T));
    }

    // Copy all component data from (srcChunk, srcRow) in srcArch
    // into (dstChunk, dstRow) in this archetype, for components present in both.
    void CopySharedComponents(
        uint32_t dstChunkIdx, uint32_t dstRowIdx,
        const Archetype& srcArch, uint32_t srcChunkIdx, uint32_t srcRowIdx)
    {
        const Chunk* src = srcArch.Chunks[srcChunkIdx].get();
        Chunk*       dst = Chunks[dstChunkIdx].get();

        for (uint32_t dc = 0; dc < dst->ColumnCount; ++dc)
        {
            const ComponentId id     = dst->Columns[dc].Id;
            const size_t      stride = dst->Columns[dc].Stride;
            if (stride == 0) continue;

            const uint32_t sc = src->FindColumn(id);
            if (sc == UINT32_MAX) continue;

            assert(src->Columns[sc].Stride == stride);
            uint8_t* dstPtr = dst->ColumnData(dc) + dstRowIdx * stride;
            const uint8_t* srcPtr = src->ColumnData(sc) + srcRowIdx * stride;
            std::memcpy(dstPtr, srcPtr, stride);
        }
    }

private:
    size_t EntityColumnOffset_ = 0;
};
