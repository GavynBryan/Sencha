#pragma once

#include <graphics/BufferHandle.h>
#include <graphics/FramesInFlight.h>
#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanShaderCache.h>
#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <span>

//=============================================================================
// SkinnedPosePass
//
// The pre-skin dispatch (pipeline Decision N, resolved 2026-08-23): one
// compute invocation per vertex blends the joint palette into the rest
// geometry and writes a posed vertex buffer that every geometry pass then
// consumes exactly like static geometry. Takes only plain data -- buffers,
// counts, byte offsets -- so all policy (which instances, which palettes,
// buffer lifecycle) stays with the render-side feature.
//
// The pass owns the descriptor story: a four-binding storage layout and one
// descriptor pool per frame in flight, reset each frame before the jobs
// allocate from it. It also owns its one barrier -- the dispatches' storage
// writes made visible to vertex-attribute reads -- because barrier placement
// is pass policy.
//
// At bind pose the palette is identity and single-influence vertices
// reproduce the rest bytes exactly (the skinned_rest golden gate). Blended
// influences carry a one-ulp weight-sum wobble, invisible and only present
// on vertices that actually blend.
//=============================================================================

// Buffer handles rather than VkBuffers: the caller is render policy and
// must not name the backend, so the pass resolves them.
struct SkinnedPoseDispatch
{
    BufferHandle RestVertices;
    BufferHandle Influences;
    BufferHandle PosedVertices;
    std::uint32_t VertexCount = 0;
    // The palette's placement inside the shared palette buffer (the frame
    // scratch ring), in bytes.
    BufferHandle PaletteBuffer;
    std::uint64_t PaletteOffset = 0;
    std::uint64_t PaletteBytes = 0;
};

class SkinnedPosePass
{
public:
    // One descriptor set per dispatch per frame; the pool is sized for this
    // many and Record drops jobs beyond it (the feature budgets first, so
    // hitting the cap here is a policy failure worth the log).
    static constexpr std::uint32_t kMaxDispatchesPerFrame = 64;

    void Setup(const RendererServices& services);
    // Posed-vertex buffers are the pass's own resource kind (GpuOnly, vertex
    // + storage), so their creation stays in the backend and the feature
    // above holds only the policy that decides when to ask for one.
    [[nodiscard]] BufferHandle CreatePosedBuffer(std::uint32_t vertexCount);
    void DestroyPosedBuffer(BufferHandle buffer);
    // Records every dispatch and the write->vertex-read barrier. Returns how
    // many jobs were recorded; the caller treats the rest as unposed.
    std::uint32_t Record(const FrameContext& frame,
                         std::span<const SkinnedPoseDispatch> dispatches);
    void Teardown();

private:
    VulkanBufferService* Buffers = nullptr;
    VulkanPipelineCache* Pipelines = nullptr;
    VulkanShaderCache* Shaders = nullptr;
    Logger* Log = nullptr;
    VkDevice Device = VK_NULL_HANDLE;
    // minStorageBufferOffsetAlignment, queried at setup. Palette offsets are
    // pre-aligned upstream against the spec's 256-byte ceiling; this is what
    // proves that upstream promise against the device actually running.
    VkDeviceSize StorageOffsetAlignment = 1;

    ShaderHandle ComputeShader;
    VkDescriptorSetLayout SetLayout = VK_NULL_HANDLE;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    std::array<VkDescriptorPool, kMaxFramesInFlight> Pools{};
};
