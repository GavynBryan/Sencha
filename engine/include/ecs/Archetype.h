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

    // Takes an emptied slab back before allocating, so resident chunk memory is
    // bounded by how many chunks are live at once rather than by how many have
    // ever been live. Streaming a zone in and out repeatedly used to orphan every
    // slab but one per cycle.
    Chunk* AllocChunk(StoragePartitionId partition = StoragePartitionId::Default())
    {
        uint32_t index;
        if (!FreeChunks_.empty())
        {
            index = FreeChunks_.back();
            FreeChunks_.pop_back();

            Chunk& recycled = *Chunks[index];
            assert(recycled.IsEmpty() && "a chunk on the free list holds rows");
            recycled.Partition = partition;
            // The previous tenant's column versions are left alone: nothing reads
            // an empty chunk, and AddRow stamps every column as this frame's write
            // when the first row lands.
        }
        else
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
            index = static_cast<uint32_t>(Chunks.size()) - 1;
        }

        LastChunkByPartition_[partition.Value] = index;
        return Chunks[index].get();
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
    //
    // A row appearing in a chunk is a write to every column of that chunk, and it
    // is stamped as one: change detection is chunk-conservative, so a consumer
    // that skips unchanged chunks would otherwise never see a newly created or
    // newly migrated entity. Every path that creates a row funnels through here so
    // none of them can forget. The frame is compared before writing because a
    // batch of rows normally lands in a chunk this frame already stamped.
    std::pair<uint32_t, uint32_t> AddRow(
        EntityIndex entityIndex,
        StoragePartitionId partition = StoragePartitionId::Default(),
        uint32_t frame = 0)
    {
        Chunk* chunk = GetOrAllocChunkWithSpace(partition);
        const uint32_t ci = LastChunkByPartition_[partition.Value];
        const uint32_t ri = chunk->RowCount++;
        chunk->EntityIndices()[ri] = entityIndex;

        for (uint32_t col = 0; col < chunk->ColumnCount; ++col)
        {
            if (chunk->ColumnLastWrittenFrame(col) != frame)
                chunk->BumpColumnVersion(col, frame);
        }

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

        // A slab that just lost its last row goes back to the free list, where any
        // partition of this archetype can claim it. Nothing outside moves: the
        // chunk keeps its index in Chunks, so no EntityLocation needs fixing up.
        // That is why reclamation is a free list rather than compaction — erasing
        // or swapping chunk slots would renumber live rows.
        if (chunk->RowCount == 0)
            ReleaseChunk(chunkIdx);

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

    // Slabs held but holding nothing: the reclamation signal. Diagnostics and
    // tests only.
    size_t FreeChunkCount() const { return FreeChunks_.size(); }

private:
    void ReleaseChunk(uint32_t chunkIdx)
    {
        Chunk& chunk = *Chunks[chunkIdx];

        // Drop the partition's claim on it, or the next row that partition adds
        // would land in a slab another partition has since taken.
        const auto claim = LastChunkByPartition_.find(chunk.Partition.Value);
        if (claim != LastChunkByPartition_.end() && claim->second == chunkIdx)
            LastChunkByPartition_.erase(claim);

        FreeChunks_.push_back(chunkIdx);
    }

    size_t EntityColumnOffset_ = 0;
    std::unordered_map<uint32_t, uint32_t> LastChunkByPartition_;

    // Indices into Chunks, most recently emptied first. Reusing the newest slab
    // first is the warmest choice for the allocator and for the cache.
    std::vector<uint32_t> FreeChunks_;
};
