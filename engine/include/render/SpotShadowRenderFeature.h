#pragma once

#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanShaderCache.h>
#include <render/RenderLight.h>
#include <render/ShadowCasterSet.h>
#include <render/SpotShadowResources.h>
#include <render/static_mesh/StaticMeshCache.h>

#include <memory>

class SpotShadowRenderFeature final : public IRenderFeature
{
public:
    SpotShadowRenderFeature(std::shared_ptr<SpotShadowResources> resources,
                            const RenderLightSet& lights,
                            const ShadowCasterSet& casters,
                            StaticMeshCache& meshes);

    [[nodiscard]] RenderPhase GetPhase() const override { return RenderPhase::Offscreen; }
    void Setup(const RendererServices& services) override;
    void PrepareFrame(const FrameContext& frame) override;
    void Record(const FrameContext& frame) override;
    void Teardown() override;

private:
    [[nodiscard]] bool EnsurePipelines();
    [[nodiscard]] bool BindInstanceStream(const FrameContext& frame);
    [[nodiscard]] VkDeviceSize UploadView(const SpotShadowView& shadow);
    void BindView(const FrameContext& frame, VkDeviceSize uniformOffset);

    std::shared_ptr<SpotShadowResources> Resources;
    const RenderLightSet& Lights;
    const ShadowCasterSet& Casters;
    StaticMeshCache& Meshes;

    VulkanBufferService* Buffers = nullptr;
    VulkanDescriptorCache* Descriptors = nullptr;
    VulkanFrameScratch* Scratch = nullptr;
    VulkanPipelineCache* PipelineCache = nullptr;
    VulkanShaderCache* Shaders = nullptr;

    ShaderHandle VertexShader;
    ShaderHandle FragmentShader;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    VkPipeline BackPipeline = VK_NULL_HANDLE;
    VkPipeline DoubleSidedPipeline = VK_NULL_HANDLE;
};
