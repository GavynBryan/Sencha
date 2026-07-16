#include <render/SpotShadowRenderFeature.h>

#include <graphics/vulkan/VulkanBufferService.h>
#include <graphics/vulkan/VulkanDescriptorCache.h>
#include <graphics/vulkan/VulkanFrameScratch.h>
#include <graphics/vulkan/VulkanPipelineCache.h>
#include <math/geometry/3d/Frustum.h>
#include <shaders/kShadowDepthFragSpv.h>
#include <shaders/kShadowDepthVertSpv.h>

#include <algorithm>
#include <cstddef>
#include <cstring>

SpotShadowRenderFeature::SpotShadowRenderFeature(
    std::shared_ptr<SpotShadowResources> resources,
    const RenderLightSet& lights,
    const ShadowCasterSet& casters,
    StaticMeshCache& meshes)
    : Resources(std::move(resources))
    , Lights(lights)
    , Casters(casters)
    , Meshes(meshes)
{
}

void SpotShadowRenderFeature::Setup(const RendererServices& services)
{
    Buffers = services.Buffers;
    Descriptors = services.Descriptors;
    Scratch = services.Scratch;
    PipelineCache = services.Pipelines;
    Shaders = services.Shaders;

    if (!Resources->Setup(services))
    {
        if (services.Logging != nullptr)
        {
            services.Logging->GetLogger<SpotShadowRenderFeature>().Warn(
                "Spot shadow resources failed to set up; spot shadows disabled");
        }
        return;
    }

    VertexShader = Shaders->CreateModuleFromSpirv(
        kShadowDepthVertSpv, kShadowDepthVertSpvWordCount, "Spot shadow vertex");
    FragmentShader = Shaders->CreateModuleFromSpirv(
        kShadowDepthFragSpv, kShadowDepthFragSpvWordCount, "Spot shadow fragment");

    PipelineLayout = Descriptors->GetDefaultPipelineLayout();
    Descriptors->SetFrameUniformBuffer(Scratch->GetBuffer(), sizeof(Mat4));
}

bool SpotShadowRenderFeature::EnsurePipelines()
{
    const float biasConstant = std::max(Lights.ShadowBiasConstant, 0.0f);
    const float biasSlope = std::max(Lights.ShadowBiasSlope, 0.0f);
    if (BackPipeline != VK_NULL_HANDLE
        && DoubleSidedPipeline != VK_NULL_HANDLE
        && CachedBiasConstant == biasConstant
        && CachedBiasSlope == biasSlope)
    {
        return true;
    }
    if (!Resources->IsValid() || PipelineLayout == VK_NULL_HANDLE)
        return false;

    GraphicsPipelineDesc base{};
    base.VertexShader = VertexShader;
    base.FragmentShader = FragmentShader;
    base.Layout = PipelineLayout;
    base.VertexBindings = {
        { 0, sizeof(StaticMeshVertex), VK_VERTEX_INPUT_RATE_VERTEX },
        { 1, sizeof(Mat4), VK_VERTEX_INPUT_RATE_INSTANCE },
    };
    base.VertexAttributes = {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(StaticMeshVertex, Position) },
        { 3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0 },
        { 4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 16 },
        { 5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 32 },
        { 6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 48 },
    };
    base.FrontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    base.DepthTest = true;
    base.DepthWrite = true;
    base.DepthCompare = VK_COMPARE_OP_LESS_OR_EQUAL;
    base.DepthBiasEnable = true;
    base.DepthBiasConstant = biasConstant;
    base.DepthBiasSlope = biasSlope;
    base.DepthFormat = VK_FORMAT_D16_UNORM;

    base.CullMode = VK_CULL_MODE_BACK_BIT;
    const VkPipeline backPipeline = PipelineCache->GetGraphicsPipeline(base);
    base.CullMode = VK_CULL_MODE_NONE;
    const VkPipeline doubleSidedPipeline = PipelineCache->GetGraphicsPipeline(base);
    if (backPipeline == VK_NULL_HANDLE || doubleSidedPipeline == VK_NULL_HANDLE)
        return false;

    BackPipeline = backPipeline;
    DoubleSidedPipeline = doubleSidedPipeline;
    CachedBiasConstant = biasConstant;
    CachedBiasSlope = biasSlope;
    return true;
}

bool SpotShadowRenderFeature::BindInstanceStream(const FrameContext& frame)
{
    auto allocation = Scratch->AllocateVertex(Casters.Items.size() * sizeof(Mat4));
    if (!allocation.IsValid())
        return false;

    auto* transforms = static_cast<Mat4*>(allocation.Mapped);
    for (std::size_t index = 0; index < Casters.Items.size(); ++index)
        transforms[index] = Casters.Items[index].WorldMatrix.Transposed();

    const VkBuffer buffer = Buffers->GetBuffer(allocation.Buffer);
    vkCmdBindVertexBuffers(frame.Cmd, 1, 1, &buffer, &allocation.Offset);
    return true;
}

VkDeviceSize SpotShadowRenderFeature::UploadView(const SpotShadowView& shadow)
{
    const Mat4 viewProjection = shadow.ViewProjection.Transposed();
    auto allocation = Scratch->AllocateUniform(sizeof(viewProjection));
    if (!allocation.IsValid())
        return VK_WHOLE_SIZE;
    std::memcpy(allocation.Mapped, &viewProjection, sizeof(viewProjection));
    return allocation.Offset;
}

void SpotShadowRenderFeature::BindView(const FrameContext& frame, VkDeviceSize uniformOffset)
{
    const std::uint32_t dynamicOffset = static_cast<std::uint32_t>(uniformOffset);
    const VkDescriptorSet frameSet = Descriptors->GetFrameSet();
    vkCmdBindDescriptorSets(frame.Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, PipelineLayout,
                            0, 1, &frameSet, 1, &dynamicOffset);
}

void SpotShadowRenderFeature::OnDraw(const FrameContext& frame)
{
    if (Lights.SpotShadowCount == 0 || !Resources->IsValid())
        return;

    const bool canDrawCasters = !Casters.Items.empty()
        && EnsurePipelines()
        && BindInstanceStream(frame);

    Resources->TransitionForWrite(frame.Cmd);

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = Resources->GetView();
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = { 1.0f, 0 };

    VkPipeline lastPipeline = VK_NULL_HANDLE;
    VkBuffer lastVertexBuffer = VK_NULL_HANDLE;
    VkBuffer lastIndexBuffer = VK_NULL_HANDLE;

    for (std::uint32_t shadowIndex = 0;
         shadowIndex < Lights.SpotShadowCount;
         ++shadowIndex)
    {
        const SpotShadowView& shadow = Lights.SpotShadows[shadowIndex];
        const std::uint32_t column = shadowIndex % kSpotShadowAtlasColumns;
        const std::uint32_t row = shadowIndex / kSpotShadowAtlasColumns;

        VkRenderingInfo rendering{};
        rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering.renderArea.offset = {
            static_cast<std::int32_t>(column * kSpotShadowTileExtent),
            static_cast<std::int32_t>(row * kSpotShadowTileExtent),
        };
        rendering.renderArea.extent = {
            kSpotShadowTileExtent,
            kSpotShadowTileExtent,
        };
        rendering.layerCount = 1;
        rendering.pDepthAttachment = &depthAttachment;
        vkCmdBeginRendering(frame.Cmd, &rendering);

        if (!canDrawCasters)
        {
            vkCmdEndRendering(frame.Cmd);
            continue;
        }

        const VkDeviceSize uniformOffset = UploadView(shadow);
        if (uniformOffset == VK_WHOLE_SIZE)
        {
            vkCmdEndRendering(frame.Cmd);
            continue;
        }
        BindView(frame, uniformOffset);

        VkViewport viewport{};
        viewport.x = static_cast<float>(
            column * kSpotShadowTileExtent + kSpotShadowGuardTexels);
        viewport.y = static_cast<float>(
            row * kSpotShadowTileExtent + kSpotShadowGuardTexels);
        viewport.width = static_cast<float>(kSpotShadowInnerExtent);
        viewport.height = static_cast<float>(kSpotShadowInnerExtent);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(frame.Cmd, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {
            static_cast<std::int32_t>(column * kSpotShadowTileExtent),
            static_cast<std::int32_t>(row * kSpotShadowTileExtent),
        };
        scissor.extent = { kSpotShadowTileExtent, kSpotShadowTileExtent };
        vkCmdSetScissor(frame.Cmd, 0, 1, &scissor);

        const Frustum shadowFrustum = Frustum::FromViewProjection(shadow.ViewProjection);
        for (std::uint32_t casterIndex = 0;
             casterIndex < static_cast<std::uint32_t>(Casters.Items.size());
             ++casterIndex)
        {
            const ShadowCasterItem& caster = Casters.Items[casterIndex];
            if (!shadowFrustum.IntersectsAabb(caster.WorldBounds))
                continue;

            const GpuStaticMesh* mesh = Meshes.Get(caster.Mesh);
            if (mesh == nullptr || caster.SectionIndex >= mesh->Sections.size())
                continue;

            const VkPipeline pipeline = caster.DoubleSided
                ? DoubleSidedPipeline
                : BackPipeline;
            if (pipeline != lastPipeline)
            {
                vkCmdBindPipeline(frame.Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                lastPipeline = pipeline;
            }

            const VkBuffer vertexBuffer = Buffers->GetBuffer(mesh->VertexBuffer);
            const VkBuffer indexBuffer = Buffers->GetBuffer(mesh->IndexBuffer);
            if (vertexBuffer != lastVertexBuffer)
            {
                VkDeviceSize vertexOffset = 0;
                vkCmdBindVertexBuffers(frame.Cmd, 0, 1, &vertexBuffer, &vertexOffset);
                lastVertexBuffer = vertexBuffer;
            }
            if (indexBuffer != lastIndexBuffer)
            {
                vkCmdBindIndexBuffer(frame.Cmd, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                lastIndexBuffer = indexBuffer;
            }

            const StaticMeshSection& section = mesh->Sections[caster.SectionIndex];
            vkCmdDrawIndexed(frame.Cmd, section.IndexCount, 1,
                             section.IndexOffset, 0, casterIndex);
        }
        vkCmdEndRendering(frame.Cmd);
    }

    Resources->TransitionForRead(frame.Cmd);
}

void SpotShadowRenderFeature::Teardown()
{
    if (Shaders != nullptr)
    {
        Shaders->Destroy(VertexShader);
        Shaders->Destroy(FragmentShader);
    }
    VertexShader = {};
    FragmentShader = {};
    BackPipeline = VK_NULL_HANDLE;
    DoubleSidedPipeline = VK_NULL_HANDLE;
    CachedBiasConstant = -1.0f;
    CachedBiasSlope = -1.0f;
    PipelineLayout = VK_NULL_HANDLE;
    Resources->Teardown();
}
