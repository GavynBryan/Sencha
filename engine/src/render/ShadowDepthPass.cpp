#include <render/ShadowDepthPass.h>

#include <graphics/vulkan/RenderScope.h>
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
    // Casters draw together when they share the shadow pipeline, the mesh
    // buffers, and the section's draw parameters: everything vkCmdDrawIndexed
    // takes except the instance range.
    [[nodiscard]] bool SameShadowDraw(const ShadowCasterItem& a,
                                      const ShadowCasterItem& b)
    {
        return a.DoubleSided == b.DoubleSided
            && a.Mesh == b.Mesh
            && a.SectionIndex == b.SectionIndex;
    }

    // Sort order matching SameShadowDraw, so equal draws land adjacent.
    [[nodiscard]] bool ShadowDrawOrderLess(const ShadowCasterItem& a,
                                           const ShadowCasterItem& b)
    {
        if (a.DoubleSided != b.DoubleSided)
            return static_cast<int>(a.DoubleSided) < static_cast<int>(b.DoubleSided);
        if (a.Mesh.Index != b.Mesh.Index)
            return a.Mesh.Index < b.Mesh.Index;
        if (a.Mesh.Generation != b.Mesh.Generation)
            return a.Mesh.Generation < b.Mesh.Generation;
        return a.SectionIndex < b.SectionIndex;
    }

    // A light cannot cast past its range, so one sphere test rejects a caster
    // for every view of that light at once.
    [[nodiscard]] bool WithinLightRange(const Vec4& sphere, const Aabb3d& bounds)
    {
        const Vec3d center(sphere.X, sphere.Y, sphere.Z);
        // Distance from the light to the closest point of the box, clamped
        // per axis.
        Vec3d delta{};
        for (int axis = 0; axis < 3; ++axis)
        {
            const double value = center[axis];
            const double closest = std::clamp(value,
                                              static_cast<double>(bounds.Min[axis]),
                                              static_cast<double>(bounds.Max[axis]));
            delta[axis] = static_cast<float>(closest - value);
        }
        const double range = static_cast<double>(sphere.W);
        return delta.SqrMagnitude() <= range * range;
    }

    // A point face job names only its cube slot, so the light's position and
    // range come from the packed set the forward pass will sample.
    [[nodiscard]] bool FindPointLightSphere(const RenderLightSet& lights,
                                            std::uint32_t slot,
                                            Vec4& out)
    {
        const std::uint32_t pointType = static_cast<std::uint32_t>(GpuLightType::Point);
        for (std::uint32_t index = 0; index < lights.Count; ++index)
        {
            const GpuLight& light = lights.Lights[index];
            if (light.Type == pointType && light.ShadowIndex == slot)
            {
                out = light.PositionRange;
                return true;
            }
        }
        return false;
    }

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

    // Compile the depth variants at load. The bias values come from cvars, so
    // this warms the defaults; a run that changes them rebuilds once through
    // EnsurePipelines rather than on the first frame that casts a shadow.
    (void)EnsurePipelines(RenderLightSet{});
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

void ShadowDepthPass::GatherVisibleCasters(const Mat4& viewProjection,
                                           const ShadowCasterSet& casters,
                                           StaticMeshCache& meshes,
                                           const Vec4* lightSphere)
{
    VisibleCasters.clear();

    const Frustum shadowFrustum = Frustum::FromViewProjection(viewProjection);
    for (std::uint32_t index = 0;
         index < static_cast<std::uint32_t>(casters.Items.size());
         ++index)
    {
        const ShadowCasterItem& caster = casters.Items[index];
        ++LastStats.CastersTested;
        if (lightSphere != nullptr && !WithinLightRange(*lightSphere, caster.WorldBounds))
            continue;
        if (!shadowFrustum.IntersectsAabb(caster.WorldBounds))
            continue;

        // Resolved here so the run walk can draw without re-checking, and so a
        // caster whose mesh is gone never opens a run.
        const GpuStaticMesh* mesh = meshes.Get(caster.Mesh);
        if (mesh == nullptr || caster.SectionIndex >= mesh->Sections.size())
            continue;

        ++LastStats.CastersVisible;
        VisibleCasters.push_back(index);
    }

    std::sort(VisibleCasters.begin(), VisibleCasters.end(),
              [&casters](std::uint32_t a, std::uint32_t b)
              {
                  return ShadowDrawOrderLess(casters.Items[a], casters.Items[b]);
              });
}

bool ShadowDepthPass::RecordView(const FrameContext& frame,
                                 const ViewTarget& target,
                                 const Mat4& viewProjection,
                                 const ShadowCasterSet& casters,
                                 StaticMeshCache& meshes,
                                 const Vec4* lightSphere,
                                 bool flipFrontFace)
{
    GatherVisibleCasters(viewProjection, casters, meshes, lightSphere);

    // Transforms for this view only, in run order, so identical draws are
    // adjacent and collapse into one instanced call.
    VulkanFrameScratch::ElementAllocation stream;
    VkDeviceSize uniformOffset = VK_WHOLE_SIZE;
    if (!VisibleCasters.empty())
    {
        uniformOffset = UploadView(viewProjection);
        if (uniformOffset == VK_WHOLE_SIZE)
        {
            // Skipped before the target is touched: old contents stay valid
            // for whatever still samples them.
            return false;
        }
        stream = Scratch->AllocateVertexElements(
            static_cast<std::uint32_t>(VisibleCasters.size()), sizeof(Mat4));
        if (!stream.IsValid())
            return false;

        auto* transforms = static_cast<Mat4*>(stream.Grant.Mapped);
        for (std::uint32_t index = 0; index < stream.Count; ++index)
        {
            transforms[index] =
                casters.Items[VisibleCasters[index]].WorldMatrix.Transposed();
        }
        LastStats.CastersDropped +=
            static_cast<std::uint32_t>(VisibleCasters.size()) - stream.Count;
    }

    RenderScopeDesc scope{};
    scope.Area = target.RenderArea;
    scope.Depth.View = target.Attachment;
    scope.Depth.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    scope.Depth.StoreOp = VK_ATTACHMENT_STORE_OP_STORE;
    scope.Depth.Clear.depthStencil = { 1.0f, 0 };
    const RenderScope rendering(frame, scope);

    // A view nothing casts into still renders: a cleared target is the
    // correct depth for "nothing occludes". The scope closes on the way out.
    if (stream.Count == 0)
        return true;
    BindView(frame, uniformOffset);

    vkCmdSetViewport(frame.Cmd, 0, 1, &target.Viewport);
    const VkRect2D scissor = target.RenderArea;
    vkCmdSetScissor(frame.Cmd, 0, 1, &scissor);

    const VkBuffer instanceBuffer = Buffers->GetBuffer(stream.Grant.Buffer);
    vkCmdBindVertexBuffers(frame.Cmd, 1, 1, &instanceBuffer, &stream.Grant.Offset);
    // The instance stream is per view, so the mesh bindings dedup'd across
    // views no longer describe this command buffer's state.
    LastPipeline = VK_NULL_HANDLE;
    LastVertexBuffer = VK_NULL_HANDLE;
    LastIndexBuffer = VK_NULL_HANDLE;

    for (std::uint32_t first = 0; first < stream.Count;)
    {
        const ShadowCasterItem& lead = casters.Items[VisibleCasters[first]];
        std::uint32_t last = first + 1;
        while (last < stream.Count
               && SameShadowDraw(lead, casters.Items[VisibleCasters[last]]))
        {
            ++last;
        }

        // Gathering proved the mesh resident and the section in range.
        const GpuStaticMesh* mesh = meshes.Get(lead.Mesh);
        const StaticMeshSection& section = mesh->Sections[lead.SectionIndex];

        const VkPipeline pipeline = lead.DoubleSided
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

        vkCmdDrawIndexed(frame.Cmd, section.IndexCount, last - first,
                         section.IndexOffset, 0, first);
        ++LastStats.CasterDraws;
        ++LastStats.InstanceRuns;
        first = last;
    }
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
    if (!casters.Items.empty() && !EnsurePipelines(lights))
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

            // Spot views cull against their own frustum only: the cone is
            // already what the frustum describes.
            if (!RecordView(frame, target, view.ViewProjection, casters, meshes,
                            nullptr, false))
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
        Vec4 pointSphere{};
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

            const Vec4* sphere =
                FindPointLightSphere(lights, face.SlotIndex, pointSphere)
                    ? &pointSphere : nullptr;
            if (target.Attachment == VK_NULL_HANDLE
                || !RecordView(frame, target, face.ViewProjection, casters, meshes,
                               sphere, true))
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
