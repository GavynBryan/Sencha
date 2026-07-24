#pragma once

#include <core/logging/LoggingProvider.h>
#include <graphics/FrameScratchRing.h>
#include <graphics/vulkan/VulkanBufferService.h>
#include <vulkan/vulkan.h>

#include <cstdint>

class VulkanDeviceService;
class VulkanPhysicalDeviceService;

//=============================================================================
// VulkanFrameScratch
//
// Per-frame bump allocator backed by one persistently-mapped host-visible
// ring buffer. Carves the buffer into `FramesInFlight` equal slices; at the
// start of each frame `BeginFrame()` rotates to the next slice and resets
// its bump cursor. Callers write directly through the returned mapped
// pointer -- there is no staging, no flush (allocation is
// HOST_ACCESS_SEQUENTIAL_WRITE), no fence on the scratch itself.
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
// and staged GPU uploads run through VulkanBufferService::Upload.
//
// The single backing BufferHandle is stable for the service's entire life
// and is what callers point VulkanDescriptorCache::SetFrameUniformBuffer
// at once during setup.
//=============================================================================

class VulkanFrameScratch
{
public:
    // Vertex and instance streams bind at this alignment.
    static constexpr VkDeviceSize kVertexAlignment = 16;

    struct Config
    {
        uint32_t FramesInFlight = 2;
        VkDeviceSize BytesPerFrame = 1024 * 1024; // 1 MB per slice by default
    };

    struct Allocation
    {
        BufferHandle Buffer;     // The ring buffer. Same for every allocation.
        VkDeviceSize Offset = 0; // Byte offset from the start of the ring.
        void* Mapped = nullptr;  // Writable pointer == ring base + Offset.

        [[nodiscard]] bool IsValid() const { return Mapped != nullptr; }
    };

    VulkanFrameScratch(LoggingProvider& logging,
                       VulkanDeviceService& device,
                       VulkanPhysicalDeviceService& physicalDevice,
                       VulkanBufferService& buffers,
                       Config config);
    ~VulkanFrameScratch();

    VulkanFrameScratch(const VulkanFrameScratch&) = delete;
    VulkanFrameScratch& operator=(const VulkanFrameScratch&) = delete;
    VulkanFrameScratch(VulkanFrameScratch&&) = delete;
    VulkanFrameScratch& operator=(VulkanFrameScratch&&) = delete;

    [[nodiscard]] bool IsValid() const { return Valid; }

    // Advance the ring to the next slice and reset its bump cursor. Call
    // exactly once per frame, before any Allocate* calls for that frame.
    void BeginFrame();

    // Generic aligned allocation. Returns an invalid Allocation if the
    // request would overflow the current frame's slice.
    [[nodiscard]] Allocation Allocate(VkDeviceSize size, VkDeviceSize alignment);

    // Aligned to the device's minUniformBufferOffsetAlignment so the
    // returned offset is a legal dynamic-UBO base.
    [[nodiscard]] Allocation AllocateUniform(VkDeviceSize size);

    // 16-byte aligned, suitable for vertex / instance streams.
    [[nodiscard]] Allocation AllocateVertex(VkDeviceSize size);

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
                                                           VkDeviceSize stride);

    // -- Accessors ----------------------------------------------------------

    [[nodiscard]] BufferHandle GetBuffer() const { return RingBuffer; }
    [[nodiscard]] VkDeviceSize GetBytesPerFrame() const { return Ring.GetBytesPerFrame(); }
    [[nodiscard]] VkDeviceSize GetUniformAlignment() const { return UniformAlignment; }
    [[nodiscard]] uint32_t GetFramesInFlight() const { return Ring.GetFramesInFlight(); }
    // Largest per-frame cursor ever reached, for sizing BytesPerFrame.
    [[nodiscard]] VkDeviceSize GetHighWaterBytes() const { return Ring.GetHighWaterBytes(); }
    // This frame's slice use, and the requests it could not serve. Both
    // reset in BeginFrame, so they describe the frame being recorded.
    [[nodiscard]] VkDeviceSize GetUsedBytes() const { return Ring.GetUsedBytes(); }
    [[nodiscard]] uint32_t GetFailedAllocationCount() const
    {
        return Ring.GetFailedAllocationCount();
    }

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
    VkDeviceSize UniformAlignment = 256;
};
