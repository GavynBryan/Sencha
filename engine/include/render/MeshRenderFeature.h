#pragma once

#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanShaderCache.h>
#include <render/Camera.h>
#include <render/Material.h>
#include <render/MeshService.h>
#include <render/RenderQueue.h>

// Per-frame uniform data uploaded to set 0, binding 0 each draw call.
struct MeshFrameUniforms
{
    Mat4 ViewProjection;
    Vec4 ViewPositionTime;
};

// Per-draw push constants: world matrix and base color.
struct MeshPushConstants
{
    Mat4 World;
    Vec4 BaseColor;
};

//=============================================================================
// MeshRenderFeature
//
// IRenderFeature that draws all opaque meshes in the RenderQueue using the
// mesh_forward shader. Runs in RenderPhase::MainColor.
//
// The pipeline is lazily created (or recreated) on the first OnDraw() call
// and whenever the swapchain color or depth format changes.
//=============================================================================
class MeshRenderFeature : public IRenderFeature
{
public:
    MeshRenderFeature(RenderQueue& queue,
                      MeshService& meshes,
                      MaterialStore& materials,
                      const CameraRenderData& camera);

    [[nodiscard]] RenderPhase GetPhase() const override { return RenderPhase::MainColor; }
    void Setup(const RendererServices& services) override;
    void OnDraw(const FrameContext& frame) override;
    void Teardown() override;

private:
    RenderQueue* Queue = nullptr;
    MeshService* Meshes = nullptr;
    MaterialStore* Materials = nullptr;
    const CameraRenderData* Camera = nullptr;

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
