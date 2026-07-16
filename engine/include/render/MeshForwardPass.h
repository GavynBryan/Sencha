#pragma once

#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanShaderCache.h>
#include <render/Camera.h>
#include <render/MaterialCache.h>
#include <render/RenderLight.h>
#include <render/RenderQueue.h>
#include <render/static_mesh/StaticMeshCache.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

struct MeshFrameUniforms
{
    Mat4 ViewProjection;
    Vec4 ViewPositionTime;
    Vec4 AmbientSky;
    Vec4 AmbientGround;
    Vec4 StyleParams;
    std::uint32_t LightCount = 0;
    std::uint32_t TonemapEnabled = 1;
    std::uint32_t Pad0 = 0;
    std::uint32_t Pad1 = 0;
    GpuLight Lights[kMaxForwardLights];
};

struct MeshPushConstants
{
    Vec4 BaseColor;
    Vec4 EmissiveFactor;
    float NormalScale = 1.0f;
    float RoughnessFactor = 1.0f;
    float MetallicFactor = 0.0f;
    float SpecularIntensity = 0.5f;
    std::uint32_t BaseColorTextureIndex = UINT32_MAX;
    std::uint32_t NormalTextureIndex = UINT32_MAX;
    std::uint32_t OrmTextureIndex = UINT32_MAX;
    std::uint32_t EmissiveTextureIndex = UINT32_MAX;
};

//=============================================================================
// MeshForwardPass
//
// Records opaque static-mesh draws through the forward material pipelines. The
// four pipelines are the cross product of shading family and cull mode. They
// share one vertex shader and one specialized fragment shader module.
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
              MaterialCache& materials,
              Vec4 tint = Vec4{ 1.0f, 1.0f, 1.0f, 1.0f });
    void Teardown();

    struct DrawStats
    {
        uint32_t QueueItems = 0;
        uint32_t DrawCalls = 0;
    };
    [[nodiscard]] DrawStats GetLastDrawStats() const { return LastStats; }

private:
    [[nodiscard]] bool EnsurePipelines(const FrameContext& frame);
    [[nodiscard]] std::optional<VkDeviceSize> UploadFrameUniforms(
        const CameraRenderData& camera, const RenderLightSet& lights);
    [[nodiscard]] bool BindInstanceStream(const FrameContext& frame, const RenderQueue& queue);
    void BindFrameState(const FrameContext& frame, VkDeviceSize uniformOffset);
    void DrawRuns(const FrameContext& frame, const RenderQueue& queue,
                  StaticMeshCache& meshes, MaterialCache& materials, Vec4 tint);

    VulkanBufferService* Buffers = nullptr;
    VulkanDescriptorCache* Descriptors = nullptr;
    VulkanFrameScratch* Scratch = nullptr;
    VulkanPipelineCache* Pipelines = nullptr;
    VulkanShaderCache* Shaders = nullptr;

    ShaderHandle VertexShader;
    ShaderHandle FragmentShader;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    std::array<VkPipeline, 4> OpaquePipelines{};
    VkFormat CachedColorFormat = VK_FORMAT_UNDEFINED;
    VkFormat CachedDepthFormat = VK_FORMAT_UNDEFINED;
    DrawStats LastStats;
};
