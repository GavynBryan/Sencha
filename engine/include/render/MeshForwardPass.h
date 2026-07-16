#pragma once

#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanShaderCache.h>
#include <render/Camera.h>
#include <render/LightBindings.h>
#include <render/MaterialCache.h>
#include <render/RenderLight.h>
#include <render/RenderQueue.h>
#include <render/static_mesh/StaticMeshCache.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

struct GpuSpotShadow
{
    Mat4 ViewProjection;
    Vec4 AtlasScaleBias;
    Vec4 SamplingParams;
};

struct MeshFrameUniforms
{
    Mat4 ViewProjection;
    Vec4 ViewPositionTime;
    Vec4 AmbientSky;
    Vec4 AmbientGround;
    Vec4 StyleParams;
    std::uint32_t LightCount = 0;
    std::uint32_t TonemapEnabled = 1;
    float ShadowDarkness = 1.0f;
    std::uint32_t Pad1 = 0;
    GpuLight Lights[kMaxForwardLights];
    std::uint32_t SpotShadowCount = 0;
    std::uint32_t ShadowPad0 = 0;
    std::uint32_t ShadowPad1 = 0;
    std::uint32_t ShadowPad2 = 0;
    GpuSpotShadow SpotShadows[kMaxSpotShadows];
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
    std::uint32_t ReceiveShadows = 1;
    std::uint32_t Pad0 = 0;
    std::uint32_t Pad1 = 0;
    std::uint32_t Pad2 = 0;
};

class MeshForwardPass
{
public:
    // `bindings` backs set 2 of the pipeline layout; it must be set up (at
    // least dummy-backed) before this call, or the pass stays inert and
    // draws nothing.
    void Setup(const RendererServices& services, LightBindings& bindings);
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
    LightBindings* Bindings = nullptr;
    VkDevice Device = VK_NULL_HANDLE;

    ShaderHandle VertexShader;
    ShaderHandle FragmentShader;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    std::array<VkPipeline, 4> OpaquePipelines{};
    VkFormat CachedColorFormat = VK_FORMAT_UNDEFINED;
    VkFormat CachedDepthFormat = VK_FORMAT_UNDEFINED;
    DrawStats LastStats;
};
