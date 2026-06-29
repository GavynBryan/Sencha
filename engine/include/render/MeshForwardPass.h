#pragma once

#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanShaderCache.h>
#include <render/Camera.h>
#include <render/MaterialCache.h>
#include <render/RenderQueue.h>
#include <render/static_mesh/StaticMeshCache.h>

#include <cstdint>

// Per-frame uniform data uploaded to set 0, binding 0 each draw call.
struct MeshFrameUniforms
{
    Mat4 ViewProjection;
    Vec4 ViewPositionTime;
};

// Per-draw push constants: world matrix and base-color material inputs.
struct MeshPushConstants
{
    Mat4 World;
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
              const RenderQueue& queue,
              StaticMeshCache& meshes,
              MaterialCache& materials);
    void Teardown();

private:
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
};
