#include <graphics/vulkan/UiDrawPass.h>

#include <assets/texture/TextureCache.h>
#include <core/logging/Logger.h>
#include <core/logging/LoggingProvider.h>
#include <graphics/GpuFrameScratch.h>
#include <graphics/vulkan/Renderer.h>
#include <graphics/vulkan/VulkanBufferService.h>
#include <graphics/vulkan/VulkanDeviceService.h>
#include <graphics/vulkan/VulkanImageService.h>
#include <graphics/vulkan/VulkanPipelineCache.h>
#include <graphics/vulkan/VulkanSamplerCache.h>
#include <graphics/vulkan/VulkanShaderCache.h>

#include <shaders/kUiFragSpv.h>
#include <shaders/kUiVertSpv.h>

#include <algorithm>
#include <cstring>

namespace
{
// Mirrors the block in ui.vert.glsl / ui.frag.glsl. One definition per stage
// would drift; this is the single mirror, and both stages read the same range.
struct UiPushConstants
{
    float Mvp[16];
    std::uint32_t TextureIndex;
    std::uint32_t Flags;
};

constexpr std::uint32_t kFlagHasTexture = 1u;
constexpr std::uint32_t kFlagStraightAlphaTexture = 2u;

// Surface pixels to clip space: x right, y down, origin top left -- which is
// what the document engine lays out in and what Vulkan's clip space already
// points at, so there is no Y flip here.
void MakeSurfaceProjection(RenderExtent surface, float out[16])
{
    const float width = surface.Width > 0 ? static_cast<float>(surface.Width) : 1.0f;
    const float height = surface.Height > 0 ? static_cast<float>(surface.Height) : 1.0f;

    // Column-major for GLSL, which is what a mat4 push constant expects.
    std::fill(out, out + 16, 0.0f);
    out[0] = 2.0f / width;
    out[5] = 2.0f / height;
    out[10] = 1.0f;
    out[12] = -1.0f;
    out[13] = -1.0f;
    out[15] = 1.0f;
}

// projection * documentTransform * translate(translation), composed here so the
// shader never has to know what order they go in.
void ComposeMvp(const float projection[16],
                const std::optional<Mat4>& transform,
                Vec2d translation,
                float out[16])
{
    // Row-major maths, column-major output: out[col * 4 + row].
    float model[4][4] = {
        { 1, 0, 0, translation.X },
        { 0, 1, 0, translation.Y },
        { 0, 0, 1, 0 },
        { 0, 0, 0, 1 },
    };

    if (transform.has_value())
    {
        float composed[4][4]{};
        for (int row = 0; row < 4; ++row)
        {
            for (int col = 0; col < 4; ++col)
            {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k)
                    sum += transform->Data[row][k] * model[k][col];
                composed[row][col] = sum;
            }
        }
        std::memcpy(model, composed, sizeof(model));
    }

    for (int col = 0; col < 4; ++col)
    {
        for (int row = 0; row < 4; ++row)
        {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k)
            {
                // projection is already column-major: element (k, row) sits at
                // [row * 4 + k]... expressed as a row-major read below.
                const float projectionRowK = projection[k * 4 + row];
                sum += projectionRowK * model[k][col];
            }
            out[col * 4 + row] = sum;
        }
    }
}

VkRect2D ClampScissor(const UiClipRect& clip, RenderExtent surface, VkExtent2D target)
{
    const std::int32_t maxWidth = static_cast<std::int32_t>(std::min<std::uint32_t>(
        surface.Width > 0 ? surface.Width : target.width, target.width));
    const std::int32_t maxHeight = static_cast<std::int32_t>(std::min<std::uint32_t>(
        surface.Height > 0 ? surface.Height : target.height, target.height));

    if (!clip.Enabled)
    {
        return VkRect2D{ { 0, 0 },
                         { static_cast<std::uint32_t>(maxWidth),
                           static_cast<std::uint32_t>(maxHeight) } };
    }

    // A scissor outside the target is a validation error, not a clipped draw,
    // so the rectangle is clamped rather than trusted.
    const std::int32_t left = std::clamp(clip.X, 0, maxWidth);
    const std::int32_t top = std::clamp(clip.Y, 0, maxHeight);
    const std::int32_t right = std::clamp(clip.X + clip.Width, left, maxWidth);
    const std::int32_t bottom = std::clamp(clip.Y + clip.Height, top, maxHeight);

    return VkRect2D{ { left, top },
                     { static_cast<std::uint32_t>(right - left),
                       static_cast<std::uint32_t>(bottom - top) } };
}
} // namespace

bool UiDrawPass::Setup(const RendererServices& services, TextureCache* textures)
{
    Services = &services;
    Textures = textures;
    Device = services.Device != nullptr ? services.Device->GetDevice() : VK_NULL_HANDLE;
    if (Device == VK_NULL_HANDLE || services.Shaders == nullptr
        || services.Descriptors == nullptr || services.Pipelines == nullptr
        || services.Samplers == nullptr)
    {
        return false;
    }

    Logger* log = services.Logging != nullptr ? &services.Logging->GetLogger<UiDrawPass>() : nullptr;

    VertexShader = services.Shaders->CreateModuleFromSpirv(
        kUiVertSpv, kUiVertSpvWordCount, "ui.vert");
    FragmentShader = services.Shaders->CreateModuleFromSpirv(
        kUiFragSpv, kUiFragSpvWordCount, "ui.frag");
    if (!VertexShader.IsValid() || !FragmentShader.IsValid())
    {
        if (log != nullptr)
            log->Error("UiDrawPass: shader modules failed; authored UI will not draw");
        return false;
    }

    // Clamp, not repeat: a glyph atlas sampled past its edge should not wrap
    // into a neighbouring glyph.
    Sampler = services.Samplers->GetLinearClamp();

    VkPushConstantRange push{};
    // Both stages read the same block: the vertex stage needs the matrix, the
    // fragment stage the texture index and flags.
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    push.offset = 0;
    push.size = sizeof(UiPushConstants);

    // Set 0 is the frame set this pass never reads, present only so the
    // bindless array keeps the set index its shader declares.
    const VkDescriptorSetLayout setLayouts[] = {
        services.Descriptors->GetFrameSetLayout(),
        services.Descriptors->GetBindlessSetLayout(),
    };
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 2;
    layoutInfo.pSetLayouts = setLayouts;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &push;
    if (vkCreatePipelineLayout(Device, &layoutInfo, nullptr, &PipelineLayout) != VK_SUCCESS)
    {
        PipelineLayout = VK_NULL_HANDLE;
        if (log != nullptr)
            log->Error("UiDrawPass: pipeline layout creation failed; the pass is inert");
        return false;
    }

    return true;
}

void UiDrawPass::Teardown()
{
    if (Device != VK_NULL_HANDLE && PipelineLayout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(Device, PipelineLayout, nullptr);
    PipelineLayout = VK_NULL_HANDLE;

    // Every generated texture, retiring or not: the device is idle by the time
    // a feature is torn down, so there is nothing left to wait for.
    if (Services != nullptr && Services->Images != nullptr && Services->Descriptors != nullptr)
    {
        for (auto& [id, texture] : Generated)
        {
            Services->Descriptors->UnregisterSampledImage(texture.Bindless);
            Services->Images->Destroy(texture.Image);
        }
        for (RetiredTexture& retired : Retiring)
        {
            Services->Descriptors->UnregisterSampledImage(retired.Texture.Bindless);
            Services->Images->Destroy(retired.Texture.Image);
        }
    }
    Generated.clear();
    Retiring.clear();
    Services = nullptr;
    Textures = nullptr;
}

VkPipeline UiDrawPass::EnsurePipeline(const FrameContext& frame, Variant variant)
{
    // One format change invalidates every variant: they all record into the
    // same scope, so none of them outlives it.
    if (PipelineColorFormat != frame.TargetFormat)
    {
        for (VkPipeline& pipeline : Pipelines)
            pipeline = VK_NULL_HANDLE;
        PipelineColorFormat = frame.TargetFormat;
    }

    VkPipeline& cached = Pipelines[static_cast<std::size_t>(variant)];
    if (cached != VK_NULL_HANDLE)
        return cached;

    GraphicsPipelineDesc desc{};
    desc.VertexShader = VertexShader;
    desc.FragmentShader = FragmentShader;
    desc.Layout = PipelineLayout;

    VertexInputBindingDesc binding{};
    binding.Binding = 0;
    binding.Stride = sizeof(UiVertex);
    binding.InputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    desc.VertexBindings.push_back(binding);

    // Matches UiVertex exactly; the static_assert on its size is what keeps
    // this honest if a field is ever added.
    desc.VertexAttributes.push_back(
        VertexInputAttributeDesc{ 0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UiVertex, Position) });
    desc.VertexAttributes.push_back(
        VertexInputAttributeDesc{ 1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UiVertex, Uv) });
    desc.VertexAttributes.push_back(
        VertexInputAttributeDesc{ 2, 0, VK_FORMAT_R8G8B8A8_UNORM,
                                  offsetof(UiVertex, ColorSrgbPremultiplied) });

    desc.Topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    desc.CullMode = VK_CULL_MODE_NONE;
    desc.FrontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    desc.PolygonMode = VK_POLYGON_MODE_FILL;

    // UI draws over the scene in document order. Depth is neither tested nor
    // written: the attachment is bound because this phase shares the swapchain
    // scope, not because anything here has a depth.
    desc.DepthTest = false;
    desc.DepthWrite = false;

    const bool writesMask = variant == Variant::MaskReplace
                         || variant == Variant::MaskIncrement;

    // Premultiplied alpha, which is what the document engine produces and what
    // the shaders preserve end to end. A mask pass writes no colour at all.
    ColorBlendAttachmentDesc blend{};
    blend.BlendEnable = true;
    blend.SrcColor = VK_BLEND_FACTOR_ONE;
    blend.DstColor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.ColorOp = VK_BLEND_OP_ADD;
    blend.SrcAlpha = VK_BLEND_FACTOR_ONE;
    blend.DstAlpha = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.AlphaOp = VK_BLEND_OP_ADD;
    blend.WriteMask = writesMask
        ? 0
        : (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
           | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT);
    desc.ColorBlend.push_back(blend);

    switch (variant)
    {
    case Variant::Colour:
        desc.StencilTest = false;
        break;
    case Variant::ColourMasked:
        // Draw only where the mask says. The reference is dynamic because it
        // counts nesting depth.
        desc.StencilTest = true;
        desc.Stencil.CompareOp = VK_COMPARE_OP_EQUAL;
        break;
    case Variant::MaskReplace:
        // Stamp the reference wherever the geometry covers: Set writes 1 over a
        // buffer cleared to 0, SetInverse writes 0 over one cleared to 1.
        desc.StencilTest = true;
        desc.Stencil.CompareOp = VK_COMPARE_OP_ALWAYS;
        desc.Stencil.PassOp = VK_STENCIL_OP_REPLACE;
        break;
    case Variant::MaskIncrement:
        // Intersect: covered pixels go up by one, so only those covered by both
        // the previous mask and this geometry reach the new reference.
        desc.StencilTest = true;
        desc.Stencil.CompareOp = VK_COMPARE_OP_ALWAYS;
        desc.Stencil.PassOp = VK_STENCIL_OP_INCREMENT_AND_CLAMP;
        break;
    case Variant::Count:
        return VK_NULL_HANDLE;
    }

    desc.ColorFormats.push_back(frame.TargetFormat);
    desc.DepthFormat = frame.DepthFormat;
    desc.StencilFormat = frame.StencilFormat;

    cached = Services->Pipelines->GetGraphicsPipeline(desc);
    return cached;
}

void UiDrawPass::ApplyUploads(const UiDrawFrame& ui)
{
    if (ui.PendingUploads.empty())
        return;

    Logger* log = Services->Logging != nullptr
        ? &Services->Logging->GetLogger<UiDrawPass>() : nullptr;

    for (const UiTextureUpload& upload : ui.PendingUploads)
    {
        if (Generated.contains(upload.Id))
            continue;

        ImageCreateInfo info{};
        // UNORM, not SRGB: the runtime converted these once on the CPU and they
        // already hold linear premultiplied values. An sRGB view would decode
        // them a second time.
        info.Format = VK_FORMAT_R8G8B8A8_UNORM;
        info.Extent = VkExtent2D{ upload.Size.Width, upload.Size.Height };
        info.Usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        info.DebugName = "ui_generated";

        ++LastStats.TextureUploads;
        LastStats.TextureUploadBytes +=
            static_cast<std::uint64_t>(upload.Size.Width) * upload.Size.Height * 4u;

        const ImageHandle image = Services->Images->Create(info);
        if (!image.IsValid())
        {
            if (log != nullptr)
                log->Error("UiDrawPass: could not create a generated UI texture");
            continue;
        }
        if (!Services->Images->Upload(image, upload.LinearPremultipliedRgba8.data(),
                                      upload.LinearPremultipliedRgba8.size()))
        {
            Services->Images->Destroy(image);
            continue;
        }

        GeneratedTexture texture;
        texture.Image = image;
        texture.Bindless = Services->Descriptors->RegisterSampledImage(image, Sampler);
        texture.Size = upload.Size;
        Generated.emplace(upload.Id, texture);
    }
}

void UiDrawPass::ApplyReleases(const UiDrawFrame& ui, const FrameContext& frame)
{
    for (const UiGeneratedTextureId id : ui.ReleasedTextures)
    {
        const auto it = Generated.find(id);
        if (it == Generated.end())
            continue;
        // Command buffers recorded in earlier frames still name this image, so
        // it is retired against the frame clock rather than destroyed here.
        Retiring.push_back(RetiredTexture{ it->second, frame.Retirement.Stamp() });
        Generated.erase(it);
    }
}

void UiDrawPass::CollectRetired(const FrameContext& frame)
{
    const auto retired = std::remove_if(Retiring.begin(), Retiring.end(),
        [&](const RetiredTexture& entry) {
            if (!frame.Retirement.IsRetired(entry.RetireStamp))
                return false;
            Services->Descriptors->UnregisterSampledImage(entry.Texture.Bindless);
            Services->Images->Destroy(entry.Texture.Image);
            return true;
        });
    Retiring.erase(retired, Retiring.end());
}

// Resets the mask inside the scope that is already open. vkCmdClearAttachments
// is the only way to clear mid-scope, and it is what the document engine's own
// backends do here: a Set or SetInverse starts a new mask rather than adding to
// whatever the previous one left.
void UiDrawPass::ClearStencil(const FrameContext& frame, const UiDrawFrame& ui, std::uint32_t value)
{
    VkClearAttachment attachment{};
    attachment.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
    attachment.clearValue.depthStencil.stencil = value;

    const std::uint32_t width = std::min(
        ui.Surface.Width > 0 ? ui.Surface.Width : frame.TargetExtent.width,
        frame.TargetExtent.width);
    const std::uint32_t height = std::min(
        ui.Surface.Height > 0 ? ui.Surface.Height : frame.TargetExtent.height,
        frame.TargetExtent.height);

    VkClearRect rect{};
    rect.rect = VkRect2D{ { 0, 0 }, { width, height } };
    rect.baseArrayLayer = 0;
    rect.layerCount = 1;

    vkCmdClearAttachments(frame.Cmd, 1, &attachment, 1, &rect);
}

void UiDrawPass::Draw(const FrameContext& frame, const UiDrawFrame& ui)
{
    if (PipelineLayout == VK_NULL_HANDLE || Services == nullptr)
        return;

    // Reset before the uploads, so a frame that draws nothing still reports the
    // textures it materialized rather than carrying last frame's number.
    LastStats = DrawStats{};

    ApplyUploads(ui);
    ApplyReleases(ui, frame);
    CollectRetired(frame);

    if (ui.Commands.empty())
        return;

    const bool stencilAvailable = frame.StencilFormat != VK_FORMAT_UNDEFINED;
    if (!stencilAvailable && !WarnedNoStencil)
    {
        WarnedNoStencil = true;
        if (Services->Logging != nullptr)
        {
            Services->Logging->GetLogger<UiDrawPass>().Warn(
                "UiDrawPass: no stencil aspect on the depth attachment, so clipping "
                "to a rounded boundary falls back to a rectangle");
        }
    }

    GpuFrameScratch* scratch = Services->Scratch;
    if (scratch == nullptr)
        return;

    float projection[16];
    MakeSurfaceProjection(ui.Surface, projection);

    const VkDescriptorSet bindlessSet = Services->Descriptors->GetBindlessSet();
    vkCmdBindDescriptorSets(frame.Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, PipelineLayout,
                            1, 1, &bindlessSet, 0, nullptr);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(frame.TargetExtent.width);
    viewport.height = static_cast<float>(frame.TargetExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(frame.Cmd, 0, 1, &viewport);

    // Which stencil value the mask currently means. Set and SetInverse reset it
    // to one; Intersect raises it, so a nested clip only passes where every
    // enclosing mask also covered.
    std::uint32_t stencilReference = 1;
    Variant boundVariant = Variant::Count;

    for (const UiDrawCommand& command : ui.Commands)
    {
        if (!command.Geometry || command.Geometry->Indices.empty())
            continue;

        const bool writesMask = command.ClipMask != UiClipMaskOp::None;
        if (writesMask && !stencilAvailable)
        {
            // Nothing to write the mask into. Skipping the write leaves the
            // subsequent colour draws unclipped, which is the documented
            // degradation rather than a wrong picture.
            continue;
        }

        // The mask geometry is drawn with the value it establishes; everything
        // after is tested against it.
        std::uint32_t drawReference = stencilReference;
        Variant variant = Variant::Colour;
        if (writesMask)
        {
            switch (command.ClipMask)
            {
            case UiClipMaskOp::Set:
                ClearStencil(frame, ui, 0);
                stencilReference = 1;
                drawReference = 1;
                variant = Variant::MaskReplace;
                break;
            case UiClipMaskOp::SetInverse:
                // Cleared to one so the area *outside* the geometry is what
                // passes; the geometry stamps zero over itself.
                ClearStencil(frame, ui, 1);
                stencilReference = 1;
                drawReference = 0;
                variant = Variant::MaskReplace;
                break;
            case UiClipMaskOp::Intersect:
                ++stencilReference;
                drawReference = stencilReference;
                variant = Variant::MaskIncrement;
                break;
            case UiClipMaskOp::None:
                break;
            }
        }
        else if (command.ClipMaskEnabled && stencilAvailable)
        {
            variant = Variant::ColourMasked;
        }

        const VkPipeline pipeline = EnsurePipeline(frame, variant);
        if (pipeline == VK_NULL_HANDLE)
            continue;
        if (variant != boundVariant)
        {
            vkCmdBindPipeline(frame.Cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            boundVariant = variant;
        }
        if (variant != Variant::Colour)
        {
            vkCmdSetStencilCompareMask(frame.Cmd, VK_STENCIL_FACE_FRONT_AND_BACK, 0xFFu);
            vkCmdSetStencilWriteMask(frame.Cmd, VK_STENCIL_FACE_FRONT_AND_BACK,
                                     writesMask ? 0xFFu : 0u);
            vkCmdSetStencilReference(frame.Cmd, VK_STENCIL_FACE_FRONT_AND_BACK, drawReference);
        }

        const UiGeometryBlob& blob = *command.Geometry;
        const auto vertexBytes = static_cast<std::uint64_t>(blob.Vertices.size() * sizeof(UiVertex));
        const auto indexBytes = static_cast<std::uint64_t>(blob.Indices.size() * sizeof(std::uint32_t));

        const auto vertices = scratch->AllocateVertex(vertexBytes, ScratchTag::UiVertices);
        const auto indices = scratch->AllocateIndex(indexBytes, ScratchTag::UiIndices);
        if (!vertices.IsValid() || !indices.IsValid())
        {
            // The ring is sized by a cvar and reports its own failures; dropping
            // the command beats a partial draw from a partial copy.
            continue;
        }
        std::memcpy(vertices.Mapped, blob.Vertices.data(), vertexBytes);
        std::memcpy(indices.Mapped, blob.Indices.data(), indexBytes);

        UiPushConstants push{};
        ComposeMvp(projection, command.Transform, command.Translation, push.Mvp);

        if (command.Texture.Kind == UiTextureKind::Content && Textures != nullptr)
        {
            push.TextureIndex = Textures->GetBindlessIndex(command.Texture.Content).Value;
            push.Flags = kFlagHasTexture | kFlagStraightAlphaTexture;
        }
        else if (command.Texture.Kind == UiTextureKind::Generated)
        {
            const auto it = Generated.find(command.Texture.Generated);
            if (it != Generated.end())
            {
                push.TextureIndex = it->second.Bindless.Value;
                push.Flags = kFlagHasTexture;
            }
        }

        vkCmdPushConstants(frame.Cmd, PipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(push), &push);

        const VkRect2D scissor = ClampScissor(command.Clip, ui.Surface, frame.TargetExtent);
        if (scissor.extent.width == 0 || scissor.extent.height == 0)
            continue;
        vkCmdSetScissor(frame.Cmd, 0, 1, &scissor);

        const VkBuffer vertexBuffer = Services->Buffers->GetBuffer(vertices.Buffer);
        vkCmdBindVertexBuffers(frame.Cmd, 0, 1, &vertexBuffer, &vertices.Offset);
        vkCmdBindIndexBuffer(frame.Cmd, Services->Buffers->GetBuffer(indices.Buffer),
                             indices.Offset, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(frame.Cmd, static_cast<std::uint32_t>(blob.Indices.size()), 1, 0, 0, 0);
        ++LastStats.DrawCalls;
        LastStats.Triangles += static_cast<std::uint32_t>(blob.Indices.size() / 3);
    }
}
