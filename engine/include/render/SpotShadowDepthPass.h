#pragma once

#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanShaderCache.h>
#include <render/LightBindings.h>
#include <render/RenderLight.h>
#include <render/ShadowCasterSet.h>
#include <render/static_mesh/StaticMeshCache.h>

//=============================================================================
// SpotShadowDepthPass
//
// Records the spot shadow atlas: one depth-only dynamic-rendering scope per
// granted fixed tile, casters frustum-culled per view and drawn through an
// instance transform stream. Factored like MeshForwardPass so the editor
// drives the identical depth path for its viewports; the game wraps it in
// SpotShadowRenderFeature. Depth-bias pipelines follow the light set's bias
// values, and every granted tile is cleared even when caster drawing cannot
// proceed, so a stale tile can never shadow the scene.
//=============================================================================
class SpotShadowDepthPass
{
public:
    void Setup(const RendererServices& services, LightBindings& bindings);
    void Draw(const FrameContext& frame,
              const RenderLightSet& lights,
              const ShadowCasterSet& casters,
              StaticMeshCache& meshes);
    void Teardown();

private:
    [[nodiscard]] bool EnsurePipelines(const RenderLightSet& lights);
    [[nodiscard]] bool BindInstanceStream(const FrameContext& frame,
                                          const ShadowCasterSet& casters);
    [[nodiscard]] VkDeviceSize UploadView(const SpotShadowView& shadow);
    void BindView(const FrameContext& frame, VkDeviceSize uniformOffset);

    LightBindings* Bindings = nullptr;
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
    float CachedBiasConstant = -1.0f;
    float CachedBiasSlope = -1.0f;
};
