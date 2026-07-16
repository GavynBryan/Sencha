#include <render/SpotShadowRenderFeature.h>

#include <graphics/vulkan/VulkanBufferService.h>
#include <graphics/vulkan/VulkanDescriptorCache.h>
#include <graphics/vulkan/VulkanFrameScratch.h>
#include <graphics/vulkan/VulkanPipelineCache.h>
#include <math/geometry/3d/Frustum.h>
#include <render/MeshForwardPass.h>
#include <shaders/kMeshForwardFragSpv.h>
#include <shaders/kMeshForwardVertSpv.h>

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
        return;

    VertexShader = Shaders->CreateModuleFromSpirv(
        kMeshForwardVertSpv, kMeshForwardVertSpvWordCount, "Spot shadow vertex");
    FragmentShader = Shaders->CreateModuleFromSpirv(
        kMeshForwardFragSpv, kMeshForwardFragSpvWordCount, "Spot shadow fragment");

    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    push.offset = 0;
    push.size = sizeof(MeshPushConstants);
    PipelineLayout = Descriptors->GetPipelineLayout({ push });
    Descriptors->SetFrameUniformBuffer(Scratch->GetBuffer(), sizeof(MeshFrameUniforms));
}

void SpotShadowRenderFeature::PrepareFrame(const FrameContext&)
{
}

bool SpotShadowRenderFeature::EnsurePipelines()
{
    if (BackPipeline != VK_NULL_HANDLE && DoubleSidedPipeline != VK_NULL_HANDLE)
        return true;
    if (!Resources->IsValid() || PipelineLayout == VK_NULL_HANDLE)
        return false;

    GraphicsPipelineDesc base{};
    base.VertexShader = VertexShader;
    base.FragmentShader = FragmentShader;
    base.Layout = PipelineLayout;
    base.FragmentSpecializationConstants = {
        ShaderSpecializationConstant{ .Id = 0, .Value = 1u }
    };
    base.VertexBindings = {
        { 0, sizeof(StaticMeshVertex), VK_VERTEX_INPUT_RATE_VERTEX },
        { 1, sizeof(Mat4), VK_VERTEX_INPUT_RATE_INSTANCE },
    };
    base.VertexAttributes = {
        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(StaticMeshVertex, Position) },
        { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(StaticMeshVertex, Normal) },
        { 2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(StaticMeshVertex, Uv0) },
        { 3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0 },
        { 4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 16 },
        { 5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 32 },
        { 6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 48 },
        { 7, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(StaticMeshVertex, Tangent) },
    };
    base.FrontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    base.DepthTest = true;
    base.DepthWrite = true;
    base.DepthCompare = VK_COMPARE_OP_LESS_OR_EQUAL;
    base.DepthBiasEnable = true;
    base.DepthBiasConstant = 1.25f;
    base.DepthBiasSlope = 1.75f;
    base.DepthFormat = VK_FORMAT_D16_UNORM;

    base.CullMode = VK_CULL_MODE_BACK_BIT;
    BackPipeline = PipelineCache->GetGraphicsPipeline(base);
    base.CullMode = VK_CULL_MODE_NONE;
    DoubleSidedPipeline = PipelineCache->GetGraphicsPipeline(base);
    return BackPipeline != VK_NULL_HANDLE && DoubleSidedPipeline != VK_NULL_HANDLE;
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
    MeshFrameUniforms uniforms{};
    uniforms.ViewProjection = shadow.ViewProjection.Transposed();
    auto allocation = Scratch->AllocateUniform(sizeof(MeshFrameUniforms));
    if (!allocation.IsValid())
        return VK_WHOLE_SIZE;
    std::memcpy(allocation.Mapped, &uniforms, sizeof(uniforms));
    return allocation.Offset;
}

void SpotShadowRenderFeature::BindView(const FrameContext& frame, VkDeviceSize uniformOffset)
{
    const std::uint32_t dynamicOffset = static_cast<std::uint32_t>(uniformOffset);
    const VkDescriptorSet frameSet = Descriptors->GetFrameSet();
    vkCmdBindDescriptorSets(frame.Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, PipelineLayout,
                            0, 1, &frameSet, 1, &dynamicOffset);
    const VkDescriptorSet bindlessSet = Descriptors->GetBindlessSet();
    vkCmdBindDescriptorSets(frame.Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, PipelineLayout,
                            1, 1, &bindlessSet, 0, nullptr);
}

void SpotShadowRenderFeature::Record(const FrameContext& frame)
{
    if (Lights.SpotShadowCount == 0 || Casters.Items.empty())
        return;
    if (!EnsurePipelines() || !BindInstanceStream(frame))
        return;

    Resources->TransitionForWrite(frame.Cmd);

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = Resources->GetView();
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = { 1.0f, 0 };

    VkRenderingInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea.extent = { kSpotShadowAtlasExtent, kSpotShadowAtlasExtent };
    rendering.layerCount = 1;
    rendering.pDepthAttachment = &depthAttachment;
    vkCmdBeginRendering(frame.Cmd, &rendering);

    MeshPushConstants push{};
    push.BaseColor = Vec4(1.0f, 1.0f, 1.0f, 1.0f);
    push.EmissiveFactor = Vec4(0.0f, 0.0f, 0.0f, 0.0f);

    VkPipeline lastPipeline = VK_NULL_HANDLE;
    VkBuffer lastVertexBuffer = VK_NULL_HANDLE;
    VkBuffer lastIndexBuffer = VK_NULL_HANDLE;

    for (std::uint32_t shadowIndex = 0;
         shadowIndex < Lights.SpotShadowCount;
         ++shadowIndex)
    {
        const SpotShadowView& shadow = Lights.SpotShadows[shadowIndex];
        const VkDeviceSize uniformOffset = UploadView(shadow);
        if (uniformOffset == VK_WHOLE_SIZE)
            continue;
        BindView(frame, uniformOffset);

        const std::uint32_t column = shadowIndex % 4u;
        const std::uint32_t row = shadowIndex / 4u;
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

            vkCmdPushConstants(frame.Cmd, PipelineLayout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(push), &push);
            const StaticMeshSection& section = mesh->Sections[caster.SectionIndex];
            vkCmdDrawIndexed(frame.Cmd, section.IndexCount, 1,
                             section.IndexOffset, 0, casterIndex);
        }
    }

    vkCmdEndRendering(frame.Cmd);
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
    PipelineLayout = VK_NULL_HANDLE;
    Resources->Teardown();
}
