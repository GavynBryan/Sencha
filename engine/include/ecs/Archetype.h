#pragma once

#include <ecs/ArchetypeSignature.h>
#include <ecs/Chunk.h>
#include <ecs/ComponentId.h>
#include <ecs/EntityId.h>
#include <ecs/StoragePartitionId.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>

// Per-component registration info needed to build an archetype's column layout.
struct ComponentInfo
{
    ComponentId Id;
    size_t      Size;      // sizeof(T); 0 for tag components
    size_t      Alignment; // alignof(T); 1 for tag components
};

// Archetype: metadata for a unique component signature.
// Owns column descriptors and a flat list of live chunks. Each chunk belongs to
// exactly one StoragePartitionId; partition lookup caches the most recent chunk
// with capacity so rows from different partitions never share storage.
// Entity location within an archetype is (ChunkIndex, RowIndex).
struct Archetype
{
    ArchetypeSignature Signature;

    // Owned here; Chunk::Columns points into this vector.
    // Do not reallocate after chunks are created.
    std::vector<ColumnDescriptor> Columns;

    std::vector<std::unique_ptr<Chunk>> Chunks;

    uint32_t RowsPerChunk = 0;
    uint32_t Id           = 0; // index into World::ArchetypeList

    // Build column layout from ComponentInfos (tags filtered out by zero Size).
    // Must be called once before creating any chunks.
    void BuildLayout(const std::vector<ComponentInfo>& components)
    {
        Columns.clear();

        size_t rowByteSize = sizeof(EntityIndex);
        for (const auto& comp : components)
            if (comp.Size > 0) rowByteSize += comp.Size;

        if (rowByteSize == sizeof(EntityIndex))
        {
            // All-tag archetype — no data columns; arbitrary capacity.
            RowsPerChunk = 64;
        }
        else
        {
            RowsPerChunk = static_cast<uint32_t>(ChunkSizeBytes / rowByteSize);
            if (RowsPerChunk == 0) RowsPerChunk = 1;
        }

        size_t offset = 0;
        for (const auto& comp : components)
        {
            if (comp.Size == 0) continue;

            offset = (offset + comp.Alignment - 1) & ~(comp.Alignment - 1);

            ColumnDescriptor desc{};
            desc.Id               = comp.Id;
            desc.Offset           = offset;
            desc.Stride           = comp.Size;
            Columns.push_back(desc);

            offset += comp.Size * RowsPerChunk;
        }

        offset = (offset + alignof(EntityIndex) - 1) & ~(alignof(EntityIndex) - 1);
        EntityColumnOffset_ = offset;

        assert(offset + sizeof(EntityIndex) * RowsPerChunk <= ChunkSizeBytes
               && "Archetype column layout exceeds chunk size");
    }

    Chunk* AllocChunk(StoragePartitionId partition = StoragePartitionId::Default())
    {
        auto chunk = std::make_unique<Chunk>();
        chunk->RowCount           = 0;
        chunk->RowCapacity        = RowsPerChunk;
        chunk->Partition          = partition;
        chunk->Columns            = Columns.data();
        chunk->ColumnCount        = static_cast<uint32_t>(Columns.size());
        chunk->LastWrittenFrames.assign(Columns.size(), 0);
        chunk->EntityColumnOffset = EntityColumnOffset_;
        Chunks.push_back(std::move(chunk));

        const uint32_t index = static_cast<uint32_t>(Chunks.size()) - 1;
        LastChunkByPartition_[partition.Value] = index;
        return Chunks.back().get();
    }

    Chunk* GetOrAllocChunkWithSpace(
        StoragePartitionId partition = StoragePartitionId::Default())
    {
        const auto found = LastChunkByPartition_.find(partition.Value);
        if (found != LastChunkByPartition_.end())
        {
            Chunk* chunk = Chunks[found->second].get();
            assert(chunk->Partition == partition);
            if (!chunk->IsFull())
                return chunk;
        }
        return AllocChunk(partition);
    }

    // Returns (chunkIndex, rowIndex).
    std::pair<uint32_t, uint32_t> AddRow(
        EntityIndex entityIndex,
        StoragePartitionId partition = StoragePartitionId::Default())
    {
        Chunk* chunk = GetOrAllocChunkWithSpace(partition);
        const uint32_t ci = LastChunkByPartition_[partition.Value];
        const uint32_t ri = chunk->RowCount++;
        chunk->EntityIndices()[ri] = entityIndex;
        return { ci, ri };
    }

    // Swap-and-pop remove. Returns the EntityIndex of the row that was moved
    // into the vacated slot (InvalidEntityIndex if the removed row was already last).
    // The caller must update EntityRegistry location for the moved entity.
    EntityIndex RemoveRow(uint32_t chunkIdx, uint32_t rowIdx)
    {
        assert(chunkIdx < Chunks.size());
        Chunk* chunk = Chunks[chunkIdx].get();
        assert(rowIdx < chunk->RowCount);

        const uint32_t lastRow = chunk->RowCount - 1;
        EntityIndex movedEntity = InvalidEntityIndex;

        if (rowIdx != lastRow)
        {
            movedEntity = chunk->EntityIndices()[lastRow];
            chunk->EntityIndices()[rowIdx] = movedEntity;

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

        // Empty chunks remain reusable by the same partition. Physical chunk
        // compaction remains a World-level concern because entity locations and
        // the per-partition last-chunk cache must be updated together.
        return movedEntity;
    }

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

    // Copy all component data shared between srcArch and this archetype.
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
            uint8_t*       dstPtr = dst->ColumnData(dc) + dstRowIdx * stride;
            const uint8_t* srcPtr = src->ColumnData(sc) + srcRowIdx * stride;
            std::memcpy(dstPtr, srcPtr, stride);
        }
    }

private:
    size_t EntityColumnOffset_ = 0;
    std::unordered_map<uint32_t, uint32_t> LastChunkByPartition_;
};
