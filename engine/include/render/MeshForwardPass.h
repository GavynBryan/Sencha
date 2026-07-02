#pragma once

#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanShaderCache.h>
#include <render/Camera.h>
#include <render/MaterialCache.h>
#include <render/RenderLight.h>
#include <render/RenderQueue.h>
#include <render/static_mesh/StaticMeshCache.h>

#include <cstddef>
#include <cstdint>
#include <optional>

// Per-frame uniform data uploaded to set 0, binding 0 each draw call. Layout is
// std140 (the GLSL side mirrors it field for field); the static_asserts in the
// .cpp lock the offsets the shader assumes.
struct MeshFrameUniforms
{
    Mat4 ViewProjection;
    Vec4 ViewPositionTime;
    Vec4 AmbientSky;     // rgb sky tint, w unused
    Vec4 AmbientGround;  // rgb ground tint, w unused
    std::uint32_t LightCount = 0;
    std::uint32_t Pad0 = 0;
    std::uint32_t Pad1 = 0;
    std::uint32_t Pad2 = 0;
    GpuLight Lights[kMaxForwardLights];
};

// Per-run push constants: base-color material inputs. The world matrix rides a
// per-instance vertex stream (see Draw), so one push covers an instanced run.
struct MeshPushConstants
{
    Vec4 BaseColor;
    uint32_t BaseColorTextureIndex = UINT32_MAX;
};

//=============================================================================
// MeshForwardPass
//
// The opaque forward mesh draw, factored out of MeshRenderFeature so the game
// feature and the editor viewports drive the same code. Draw() records into the
// caller's already-open frame scope (it opens no pass of its own): it uploads a
// per-frame camera uniform, binds the frame + bindless sets, and walks the
// queue's OpaqueOrder() drawing GpuStaticMesh sections with the mesh_forward
// shader.
//
// The pipeline is lazily created (or recreated) on the first Draw() and
// whenever the target color or depth format changes. One pass instance per
// output format stays cheap (the game's swapchain vs the editor's offscreen
// target), so each owner keeps its own.
//=============================================================================
class MeshForwardPass
{
public:
    void Setup(const RendererServices& services);
    void Draw(const FrameContext& frame,
              const CameraRenderData& camera,
              const RenderLightSet& lights,
              const RenderQueue& queue,
              StaticMeshCache& meshes,
              MaterialCache& materials);
    void Teardown();

    // Last Draw()'s measurements: queue items in vs instanced draw calls out
    // (the draw-call reduction instancing bought). For profiling and tests.
    struct DrawStats
    {
        uint32_t QueueItems = 0;
        uint32_t DrawCalls = 0;
    };
    [[nodiscard]] DrawStats GetLastDrawStats() const { return LastStats; }

private:
    // Draw() stages, in call order: (re)build the pipeline for the target
    // formats, upload the per-frame uniform block, write + bind the per-instance
    // world-matrix stream in draw order, then record the per-run draws.
    [[nodiscard]] bool EnsurePipeline(const FrameContext& frame);
    [[nodiscard]] std::optional<VkDeviceSize> UploadFrameUniforms(const CameraRenderData& camera,
                                                                  const RenderLightSet& lights);
    [[nodiscard]] bool BindInstanceStream(const FrameContext& frame, const RenderQueue& queue);
    void BindFrameState(const FrameContext& frame, VkDeviceSize uniformOffset);
    void DrawRuns(const FrameContext& frame, const RenderQueue& queue,
                  StaticMeshCache& meshes, MaterialCache& materials);

    VulkanBufferService* Buffers = nullptr;
    VulkanDescriptorCache* Descriptors = nullptr;
    VulkanFrameScratch* Scratch = nullptr;
    VulkanPipelineCache* Pipelines = nullptr;
    VulkanShaderCache* Shaders = nullptr;
    VkDevice Device = VK_NULL_HANDLE;

    ShaderHandle VertexShader;
    ShaderHandle FragmentShader;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    VkPipeline Pipeline = VK_NULL_HANDLE;
    VkFormat CachedColorFormat = VK_FORMAT_UNDEFINED;
    VkFormat CachedDepthFormat = VK_FORMAT_UNDEFINED;
    DrawStats LastStats;
};
