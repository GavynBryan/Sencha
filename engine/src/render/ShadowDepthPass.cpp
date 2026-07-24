#include <render/ShadowDepthPass.h>

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

namespace
{
    void RevokeGrant(RenderLightSet& lights, GpuLightType type,
                     std::uint32_t slot)
    {
        const std::uint32_t packedType = static_cast<std::uint32_t>(type);
        for (std::uint32_t index = 0; index < lights.Count; ++index)
        {
            GpuLight& light = lights.Lights[index];
            if (light.Type == packedType && light.ShadowIndex == slot)
                light.ShadowIndex = UINT32_MAX;
        }
    }
}

void ShadowDepthPass::Setup(const RendererServices& services, LightBindings& bindings)
{
    Bindings = &bindings;
    Buffers = services.Buffers;
    Descriptors = services.Descriptors;
    Scratch = services.Scratch;
    PipelineCache = services.Pipelines;
    Shaders = services.Shaders;

    VertexShader = Shaders->CreateModuleFromSpirv(
        kShadowDepthVertSpv, kShadowDepthVertSpvWordCount, "Shadow depth vertex");
    FragmentShader = Shaders->CreateModuleFromSpirv(
        kShadowDepthFragSpv, kShadowDepthFragSpvWordCount, "Shadow depth fragment");

    PipelineLayout = Descriptors->GetDefaultPipelineLayout();
    Descriptors->SetFrameUniformBuffer(Scratch->GetBuffer(), sizeof(Mat4));
}

bool ShadowDepthPass::EnsurePipelines(const RenderLightSet& lights)
{
    const float biasConstant = std::max(lights.ShadowBiasConstant, 0.0f);
    const float biasSlope = std::max(lights.ShadowBiasSlope, 0.0f);
    if (BackPipeline != VK_NULL_HANDLE
        && FlippedBackPipeline != VK_NULL_HANDLE
        && DoubleSidedPipeline != VK_NULL_HANDLE
        && CachedBiasConstant == biasConstant
        && CachedBiasSlope == biasSlope)
    {
        return true;
    }
    if (PipelineLayout == VK_NULL_HANDLE)
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
    base.FrontFace = VK_FRONT_FACE_CLOCKWISE;
    const VkPipeline flippedBackPipeline = PipelineCache->GetGraphicsPipeline(base);
    base.FrontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    base.CullMode = VK_CULL_MODE_NONE;
    const VkPipeline doubleSidedPipeline = PipelineCache->GetGraphicsPipeline(base);
    if (backPipeline == VK_NULL_HANDLE
        || flippedBackPipeline == VK_NULL_HANDLE
        || doubleSidedPipeline == VK_NULL_HANDLE)
    {
        return false;
    }

    BackPipeline = backPipeline;
    FlippedBackPipeline = flippedBackPipeline;
    DoubleSidedPipeline = doubleSidedPipeline;
    CachedBiasConstant = biasConstant;
    CachedBiasSlope = biasSlope;
    return true;
}

bool ShadowDepthPass::BindInstanceStream(const FrameContext& frame,
                                         const ShadowCasterSet& casters)
{
    auto allocation = Scratch->AllocateVertex(casters.Items.size() * sizeof(Mat4));
    if (!allocation.IsValid())
        return false;

    auto* transforms = static_cast<Mat4*>(allocation.Mapped);
    for (std::size_t index = 0; index < casters.Items.size(); ++index)
        transforms[index] = casters.Items[index].WorldMatrix.Transposed();

    const VkBuffer buffer = Buffers->GetBuffer(allocation.Buffer);
    vkCmdBindVertexBuffers(frame.Cmd, 1, 1, &buffer, &allocation.Offset);
    return true;
}

VkDeviceSize ShadowDepthPass::UploadView(const Mat4& viewProjection)
{
    const Mat4 transposed = viewProjection.Transposed();
    auto allocation = Scratch->AllocateUniform(sizeof(transposed));
    if (!allocation.IsValid())
        return VK_WHOLE_SIZE;
    std::memcpy(allocation.Mapped, &transposed, sizeof(transposed));
    return allocation.Offset;
}

void ShadowDepthPass::BindView(const FrameContext& frame, VkDeviceSize uniformOffset)
{
    const std::uint32_t dynamicOffset = static_cast<std::uint32_t>(uniformOffset);
    const VkDescriptorSet frameSet = Descriptors->GetFrameSet();
    vkCmdBindDescriptorSets(frame.Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, PipelineLayout,
                            0, 1, &frameSet, 1, &dynamicOffset);
}

bool ShadowDepthPass::RecordView(const FrameContext& frame,
                                 const ViewTarget& target,
                                 const Mat4& viewProjection,
                                 const ShadowCasterSet& casters,
                                 StaticMeshCache& meshes,
                                 bool canDrawCasters,
                                 bool flipFrontFace)
{
    VkDeviceSize uniformOffset = VK_WHOLE_SIZE;
    if (canDrawCasters)
    {
        uniformOffset = UploadView(viewProjection);
        if (uniformOffset == VK_WHOLE_SIZE)
        {
            // Skipped before the target is touched: old contents stay valid
            // for whatever still samples them.
            return false;
        }
    }

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = target.Attachment;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = { 1.0f, 0 };

    VkRenderingInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea = target.RenderArea;
    rendering.layerCount = 1;
    rendering.pDepthAttachment = &depthAttachment;
    vkCmdBeginRendering(frame.Cmd, &rendering);

    // An empty caster set still renders the view: a cleared target is the
    // correct depth for "nothing occludes".
    if (!canDrawCasters)
    {
        vkCmdEndRendering(frame.Cmd);
        return true;
    }
    BindView(frame, uniformOffset);

    vkCmdSetViewport(frame.Cmd, 0, 1, &target.Viewport);
    const VkRect2D scissor = target.RenderArea;
    vkCmdSetScissor(frame.Cmd, 0, 1, &scissor);

    const Frustum shadowFrustum = Frustum::FromViewProjection(viewProjection);
    for (std::uint32_t casterIndex = 0;
         casterIndex < static_cast<std::uint32_t>(casters.Items.size());
         ++casterIndex)
    {
        const ShadowCasterItem& caster = casters.Items[casterIndex];
        ++LastStats.CastersTested;
        if (!shadowFrustum.IntersectsAabb(caster.WorldBounds))
            continue;
        ++LastStats.CastersVisible;

        const GpuStaticMesh* mesh = meshes.Get(caster.Mesh);
        if (mesh == nullptr || caster.SectionIndex >= mesh->Sections.size())
            continue;

        const VkPipeline pipeline = caster.DoubleSided
            ? DoubleSidedPipeline
            : (flipFrontFace ? FlippedBackPipeline : BackPipeline);
        if (pipeline != LastPipeline)
        {
            vkCmdBindPipeline(frame.Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            LastPipeline = pipeline;
        }

        const VkBuffer vertexBuffer = Buffers->GetBuffer(mesh->VertexBuffer);
        const VkBuffer indexBuffer = Buffers->GetBuffer(mesh->IndexBuffer);
        if (vertexBuffer != LastVertexBuffer)
        {
            VkDeviceSize vertexOffset = 0;
            vkCmdBindVertexBuffers(frame.Cmd, 0, 1, &vertexBuffer, &vertexOffset);
            LastVertexBuffer = vertexBuffer;
        }
        if (indexBuffer != LastIndexBuffer)
        {
            vkCmdBindIndexBuffer(frame.Cmd, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
            LastIndexBuffer = indexBuffer;
        }

        const StaticMeshSection& section = mesh->Sections[caster.SectionIndex];
        vkCmdDrawIndexed(frame.Cmd, section.IndexCount, 1,
                         section.IndexOffset, 0, casterIndex);
        ++LastStats.CasterDraws;
    }
    vkCmdEndRendering(frame.Cmd);
    return true;
}

void ShadowDepthPass::Draw(const FrameContext& frame,
                           RenderLightSet& lights,
                           std::span<const SpotShadowViewJob> views,
                           std::span<const PointShadowFaceJob> pointFaces,
                           const ShadowCasterSet& casters,
                           StaticMeshCache& meshes,
                           ShadowResidency* residency)
{
    LastStats = DrawStats{};
    if (Bindings == nullptr)
        return;
    const bool drawSpots = Bindings->HasAtlas() && !views.empty();
    const bool drawPoints = Bindings->HasCubePool() && !pointFaces.empty();
    if (!drawSpots && !drawPoints)
        return;

    // Only a recording failure leaves targets untouched, reported so cached
    // content is not sampled as fresh.
    const bool canDrawCasters = !casters.Items.empty();
    if (canDrawCasters
        && (!EnsurePipelines(lights) || !BindInstanceStream(frame, casters)))
    {
        for (const SpotShadowViewJob& view : views)
        {
            if (residency != nullptr)
                residency->MarkViewFailed(view.SlotIndex);
            RevokeGrant(lights, GpuLightType::Spot, view.SlotIndex);
        }
        for (const PointShadowFaceJob& face : pointFaces)
        {
            if (residency != nullptr)
                residency->MarkPointFaceFailed(face.SlotIndex, face.Face);
            RevokeGrant(lights, GpuLightType::Point, face.SlotIndex);
        }
        LastStats.Skipped = true;
        return;
    }

    LastPipeline = VK_NULL_HANDLE;
    LastVertexBuffer = VK_NULL_HANDLE;
    LastIndexBuffer = VK_NULL_HANDLE;

    if (drawSpots)
    {
        Bindings->TransitionAtlasForWrite(frame.Cmd);
        for (const SpotShadowViewJob& view : views)
        {
            ViewTarget target;
            target.Attachment = Bindings->GetAtlasView();
            target.RenderArea.offset = {
                static_cast<std::int32_t>(view.Allocation.X),
                static_cast<std::int32_t>(view.Allocation.Y),
            };
            target.RenderArea.extent = { view.Allocation.Size, view.Allocation.Size };
            target.Viewport.x = static_cast<float>(
                view.Allocation.X + kSpotShadowGuardTexels);
            target.Viewport.y = static_cast<float>(
                view.Allocation.Y + kSpotShadowGuardTexels);
            target.Viewport.width = static_cast<float>(
                view.Allocation.Size - 2u * kSpotShadowGuardTexels);
            target.Viewport.height = target.Viewport.width;
            target.Viewport.minDepth = 0.0f;
            target.Viewport.maxDepth = 1.0f;

            if (!RecordView(frame, target, view.ViewProjection, casters, meshes,
                            canDrawCasters, false))
            {
                if (residency != nullptr)
                    residency->MarkViewFailed(view.SlotIndex);
                RevokeGrant(lights, GpuLightType::Spot, view.SlotIndex);
                continue;
            }
            ++LastStats.ViewsRendered;
        }
        Bindings->TransitionAtlasForRead(frame.Cmd);
    }

    if (drawPoints)
    {
        Bindings->TransitionCubePoolForWrite(frame.Cmd);
        for (const PointShadowFaceJob& face : pointFaces)
        {
            ViewTarget target;
            target.Attachment = Bindings->GetCubeFaceView(face.SlotIndex, face.Face);
            target.RenderArea.extent = {
                kPointShadowFaceExtent, kPointShadowFaceExtent };
            target.Viewport.width = static_cast<float>(kPointShadowFaceExtent);
            target.Viewport.height = static_cast<float>(kPointShadowFaceExtent);
            target.Viewport.minDepth = 0.0f;
            target.Viewport.maxDepth = 1.0f;

            if (target.Attachment == VK_NULL_HANDLE
                || !RecordView(frame, target, face.ViewProjection, casters, meshes,
                               canDrawCasters, true))
            {
                if (residency != nullptr)
                    residency->MarkPointFaceFailed(face.SlotIndex, face.Face);
                RevokeGrant(lights, GpuLightType::Point, face.SlotIndex);
                continue;
            }
            ++LastStats.PointFacesRendered;
        }
        Bindings->TransitionCubePoolForRead(frame.Cmd);
    }
}

void ShadowDepthPass::Teardown()
{
    if (Shaders != nullptr)
    {
        Shaders->Destroy(VertexShader);
        Shaders->Destroy(FragmentShader);
    }
    VertexShader = {};
    FragmentShader = {};
    BackPipeline = VK_NULL_HANDLE;
    FlippedBackPipeline = VK_NULL_HANDLE;
    DoubleSidedPipeline = VK_NULL_HANDLE;
    CachedBiasConstant = -1.0f;
    CachedBiasSlope = -1.0f;
    PipelineLayout = VK_NULL_HANDLE;
    Bindings = nullptr;
}
