#pragma once

#include <core/logging/LoggingProvider.h>
#include <graphics/BufferHandle.h>
#include <graphics/FrameScratchRing.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

class VulkanBufferService;
class VulkanDeviceService;
class VulkanPhysicalDeviceService;

// Which consumer an allocation is for. One slice serves skin palettes, shadow
// matrices, editor view uniforms, instance streams, and forward view uniforms,
// consumed in feature order; when it runs out, the pass that happens to be
// last silently draws less. Aggregate counters cannot say which consumer took
// the budget, so every allocation names itself.
//
// A closed enum rather than a string: the set is known, it indexes the counter
// arrays, and it keeps capture columns stable across runs. Names are
// mechanical -- ImmediateVertices is the immediate-mode vertex stream, not
// "the editor".
enum class ScratchTag : std::uint8_t
{
    SkinningPalettes,
    ShadowViewUniforms,
    ShadowInstanceTransforms,
    ForwardViewUniforms,
    ForwardInstanceData,
    ImmediateVertices,
    Count,
};

[[nodiscard]] std::string_view ToString(ScratchTag tag);

// Per-consumer slice accounting. Used and failure counts describe the frame
// being recorded; the high-water mark persists, which is what sizing a slice
// wants. Pure, so the policy is testable without a device.
class ScratchTagCounters
{
public:
    static constexpr std::size_t kTagCount = static_cast<std::size_t>(ScratchTag::Count);

    void BeginFrame()
    {
        for (std::size_t i = 0; i < kTagCount; ++i)
        {
            Used[i] = 0;
            Failed[i] = 0;
        }
    }

    void RecordGrant(ScratchTag tag, std::uint64_t bytes)
    {
        const auto i = static_cast<std::size_t>(tag);
        if (i >= kTagCount) return;
        Used[i] += bytes;
        if (Used[i] > HighWater[i])
            HighWater[i] = Used[i];
    }

    void RecordFailure(ScratchTag tag)
    {
        const auto i = static_cast<std::size_t>(tag);
        if (i < kTagCount)
            ++Failed[i];
    }

    [[nodiscard]] std::uint64_t UsedBytes(ScratchTag tag) const
    {
        const auto i = static_cast<std::size_t>(tag);
        return i < kTagCount ? Used[i] : 0;
    }
    [[nodiscard]] std::uint64_t HighWaterBytes(ScratchTag tag) const
    {
        const auto i = static_cast<std::size_t>(tag);
        return i < kTagCount ? HighWater[i] : 0;
    }
    [[nodiscard]] std::uint32_t FailedAllocations(ScratchTag tag) const
    {
        const auto i = static_cast<std::size_t>(tag);
        return i < kTagCount ? Failed[i] : 0;
    }

private:
    std::uint64_t Used[kTagCount]{};
    std::uint64_t HighWater[kTagCount]{};
    std::uint32_t Failed[kTagCount]{};
};

//=============================================================================
// GpuFrameScratch
//
// Per-frame bump allocator backed by one persistently-mapped host-visible
// ring buffer. Carves the buffer into `FramesInFlight` equal slices; at the
// start of each frame `BeginFrame()` rotates to the next slice and resets
// its bump cursor. Callers write directly through the returned mapped
// pointer -- there is no staging, no flush, no fence on the scratch itself.
// The no-flush part rests on the buffer being coherent, which the backend
// buffer service requires for host-visible allocations.
//
// Typical uses:
//   - Per-draw UBOs surfaced to VulkanDescriptorCache's dynamic UBO binding
//   - Small instance streams feeding vertex buffers for per-sprite data
//   - Transient structured data for a single compute dispatch
//
// Frame-level synchronization is the caller's problem. `FramesInFlight`
// must be at least as large as whatever frame-in-flight count the render
// loop uses -- the allocator trusts that a slice has been fully consumed
// by the GPU before its turn in the ring comes around again.
//
// The backing buffer's usage flags are UNIFORM | STORAGE | VERTEX. Index
// buffers and transfer-src are out of scope: indices are usually static,
// and staged GPU uploads run through the buffer service.
//
// The single backing BufferHandle is stable for the service's entire life,
// which is what lets the renderer point VulkanDescriptorCache's frame binding
// at it once and leave passes to declare only the range they need.
//
// The interface is backend-neutral -- handles, offsets, mapped pointers.
// Construction and the ring's memory are backend work, defined in
// src/graphics/vulkan/GpuFrameScratch.cpp.
//=============================================================================

class GpuFrameScratch
{
public:
    // Vertex and instance streams bind at this alignment.
    static constexpr std::uint64_t kVertexAlignment = 16;

    struct Config
    {
        uint32_t FramesInFlight = 2;
        std::uint64_t BytesPerFrame = 1024 * 1024; // 1 MB per slice by default
    };

    struct Allocation
    {
        BufferHandle Buffer;       // The ring buffer. Same for every allocation.
        std::uint64_t Offset = 0;  // Byte offset from the start of the ring.
        void* Mapped = nullptr;    // Writable pointer == ring base + Offset.

        [[nodiscard]] bool IsValid() const { return Mapped != nullptr; }
    };

    GpuFrameScratch(LoggingProvider& logging,
                    VulkanDeviceService& device,
                    VulkanPhysicalDeviceService& physicalDevice,
                    VulkanBufferService& buffers,
                    Config config);
    ~GpuFrameScratch();

    GpuFrameScratch(const GpuFrameScratch&) = delete;
    GpuFrameScratch& operator=(const GpuFrameScratch&) = delete;
    GpuFrameScratch(GpuFrameScratch&&) = delete;
    GpuFrameScratch& operator=(GpuFrameScratch&&) = delete;

    [[nodiscard]] bool IsValid() const { return Valid; }

    // Advance the ring to the next slice and reset its bump cursor. Call
    // exactly once per frame, before any Allocate* calls for that frame.
    void BeginFrame();

    // Generic aligned allocation. Returns an invalid Allocation if the
    // request would overflow the current frame's slice.
    [[nodiscard]] Allocation Allocate(std::uint64_t size, std::uint64_t alignment,
                                      ScratchTag tag);

    // Aligned to the device's minUniformBufferOffsetAlignment so the
    // returned offset is a legal dynamic-UBO base.
    [[nodiscard]] Allocation AllocateUniform(std::uint64_t size, ScratchTag tag);

    // 16-byte aligned, suitable for vertex / instance streams.
    [[nodiscard]] Allocation AllocateVertex(std::uint64_t size, ScratchTag tag);

    // A partial grant: `Count` elements were served, which may be fewer than
    // asked for. Zero means the slice had no room at all.
    struct ElementAllocation
    {
        Allocation Grant;
        uint32_t Count = 0;

        [[nodiscard]] bool IsValid() const { return Count > 0 && Grant.IsValid(); }
    };

    // Grants room for as many `stride`-sized elements as the current slice can
    // still serve, up to `maxElements`. An instance stream that does not fit
    // whole is the normal case at scene scale, and an all-or-nothing request
    // there means the caller drops the entire pass; this lets it draw what fits
    // and come back for the rest. Only a zero grant counts as a failure.
    [[nodiscard]] ElementAllocation AllocateVertexElements(uint32_t maxElements,
                                                           std::uint64_t stride,
                                                           ScratchTag tag);

    // -- Accessors ----------------------------------------------------------

    [[nodiscard]] BufferHandle GetBuffer() const { return RingBuffer; }
    [[nodiscard]] std::uint64_t GetBytesPerFrame() const { return Ring.GetBytesPerFrame(); }
    [[nodiscard]] std::uint64_t GetUniformAlignment() const { return UniformAlignment; }
    [[nodiscard]] uint32_t GetFramesInFlight() const { return Ring.GetFramesInFlight(); }
    // Largest per-frame cursor ever reached, for sizing BytesPerFrame.
    [[nodiscard]] std::uint64_t GetHighWaterBytes() const { return Ring.GetHighWaterBytes(); }
    // This frame's slice use, and the requests it could not serve. Both
    // reset in BeginFrame, so they describe the frame being recorded.
    [[nodiscard]] std::uint64_t GetUsedBytes() const { return Ring.GetUsedBytes(); }
    [[nodiscard]] uint32_t GetFailedAllocationCount() const
    {
        return Ring.GetFailedAllocationCount();
    }
    // The same numbers broken down by consumer, which is what names the cause
    // when a slice runs out.
    [[nodiscard]] const ScratchTagCounters& GetTagCounters() const { return TagCounters; }

private:
    // Turns a ring offset into a binding plus a writable pointer.
    [[nodiscard]] Allocation MakeAllocation(const FrameScratchRing::Grant& grant) const;

    Logger& Log;
    VulkanBufferService* Buffers = nullptr;
    bool Valid = false;

    BufferHandle RingBuffer;
    void* MappedBase = nullptr;

    // Slice geometry and cursors; this type owns only the memory behind them.
    FrameScratchRing Ring;
    ScratchTagCounters TagCounters;
    std::uint64_t UniformAlignment = 256;
};
