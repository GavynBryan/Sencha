#include <graphics/vulkan/OutlineMaskTargets.h>

#include <graphics/vulkan/RenderScope.h>

#include <graphics/vulkan/VulkanSamplerCache.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
}

void RecordOutlineMask(const FrameContext& frame, const OutlineMaskAttachments& attachments,
                       const std::function<void(const FrameContext&)>& draw)
{
    RenderTargetSession session(frame.Cmd, attachments.MaskImage, *attachments.MaskAccess);

    RenderScopeDesc desc{};
    desc.Area.extent = attachments.Extent;
    desc.Color.View = attachments.MaskView;
    desc.Color.LoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    desc.ColorFormat = kOutlineMaskFormat;
    // Read only: the mask tests against the scene's depth and writes none, and a
    // store would count as a write a later reader in the phase must be ordered after.
    desc.Depth.View = attachments.DepthView;
    desc.Depth.LoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    desc.Depth.StoreOp = VK_ATTACHMENT_STORE_OP_NONE;
    desc.DepthFormat = attachments.DepthFormat;
    if (attachments.StencilFormat != VK_FORMAT_UNDEFINED)
    {
        desc.Stencil.View = attachments.DepthView;
        desc.Stencil.LoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        desc.Stencil.StoreOp = VK_ATTACHMENT_STORE_OP_NONE;
        desc.StencilFormat = attachments.StencilFormat;
    }
    desc.Phase = RenderPhase::Post;
    const RenderScope scope(frame, desc);
    draw(scope.Context());
}

VkRect2D OutlineScissorFor(const OutlineCompositeInputs& outlines, VkExtent2D extent)
{
    // Nothing outlined is on screen: every item is wholly behind the eye.
    if (outlines.ScreenMin[0] > outlines.ScreenMax[0] || outlines.ScreenMin[1] > outlines.ScreenMax[1])
        return VkRect2D{};
    // Grown by the widest style, which is how far an outline reaches past its object.
    const auto width = static_cast<float>(extent.width);
    const auto height = static_cast<float>(extent.height);
    const float reach = outlines.MaxWidthPixels + 1.0f;
    const float left = std::clamp((outlines.ScreenMin[0] * 0.5f + 0.5f) * width - reach, 0.0f, width);
    const float top = std::clamp((outlines.ScreenMin[1] * 0.5f + 0.5f) * height - reach, 0.0f, height);
    const float right = std::clamp((outlines.ScreenMax[0] * 0.5f + 0.5f) * width + reach, 0.0f, width);
    const float bottom = std::clamp((outlines.ScreenMax[1] * 0.5f + 0.5f) * height + reach, 0.0f, height);
    return VkRect2D{ { static_cast<std::int32_t>(std::floor(left)), static_cast<std::int32_t>(std::floor(top)) },
                     { static_cast<std::uint32_t>(std::ceil(right) - std::floor(left)),
                       static_cast<std::uint32_t>(std::ceil(bottom) - std::floor(top)) } };
}

std::array<float, 4> OutlineDepthTermsOf(const Mat4& projection)
{
    return { projection[2][2], projection[2][3], projection[3][2], projection[3][3] };
}

void OutlineStyleImage::Setup(const RendererServices& services)
{
    Services = services;
}

void OutlineStyleImage::Teardown()
{
    if (Bindless.IsValid())
        Services.Descriptors->UnregisterSampledImage(Bindless);
    if (!Image.IsNull())
        Services.Images->Destroy(Image);
    Bindless = {};
    Image = {};
    Uploaded.reset();
}

std::uint32_t OutlineStyleImage::Update(const OutlineStyleTable& styles)
{
    // Compared by content: a display's scale moves the widths without the styles changing.
    if (!Image.IsNull() && Uploaded == styles)
        return Bindless.IsValid() ? Bindless.Value : UINT32_MAX;
    // A frame still in flight may be sampling the table it was given, so new styles
    // go into a new image, and the old one retires through the deletion queue and
    // the bindless gate rather than being written under that frame.
    Teardown();

    ImageCreateInfo info;
    info.Format = VK_FORMAT_R32G32B32A32_SFLOAT;
    info.Extent = { static_cast<std::uint32_t>(styles.size()) * 2, 1 };
    info.Usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.DebugName = "outline_styles";
    Image = Services.Images->Create(info);
    if (Image.IsNull())
        return UINT32_MAX;

    // Two texels per slot, read by texelFetch in outline_composite.frag.glsl:
    // colour and width, then priority.
    std::array<float, (kMaxOutlineStyles + 1) * 8> texels{};
    for (std::size_t slot = 0; slot < styles.size(); ++slot)
    {
        const OutlineStyleEntry& style = styles[slot];
        texels[slot * 8 + 0] = style.LinearColor.X;
        texels[slot * 8 + 1] = style.LinearColor.Y;
        texels[slot * 8 + 2] = style.LinearColor.Z;
        texels[slot * 8 + 3] = style.WidthPixels;
        texels[slot * 8 + 4] = static_cast<float>(style.Priority);
    }
    if (!Services.Images->Upload(Image, texels.data(), sizeof(texels)))
        return UINT32_MAX;
    Uploaded = styles;
    Bindless = Services.Descriptors->RegisterSampledImage(Image, Services.Samplers->GetNearestClamp());
    return Bindless.IsValid() ? Bindless.Value : UINT32_MAX;
}

void OutlineMaskTargets::Setup(const RendererServices& services)
{
    Targets.Setup(services);
    RenderTargetDesc mask{};
    mask.ColorFormat = kOutlineMaskFormat;
    mask.Read = RenderTargetRead::Bindless;
    mask.Sampler = services.Samplers->GetNearestClamp();
    mask.DebugName = "outline_mask";
    mask.Lifetime = RenderTargetLifetime::WithinFrame;
    Mask = Targets.Create(mask);
    RenderTargetDesc tiles = mask;
    tiles.ColorFormat = kOutlineTileFormat;
    tiles.DebugName = "outline_tiles_marked";
    MarkedTiles = Targets.Create(tiles);
    tiles.DebugName = "outline_tiles_near";
    NearTiles = Targets.Create(tiles);
    (void)TilePass.Setup(services);
    StyleImage.Setup(services);
}

void OutlineMaskTargets::Teardown()
{
    StyleImage.Teardown();
    TilePass.Teardown();
    Targets.Teardown();
}

void OutlineMaskTargets::NoteQuietFrame()
{
    if (!Quiet.NoteQuiet())
        return;
    Targets.Evict(Mask);
    Targets.Evict(MarkedTiles);
    Targets.Evict(NearTiles);
}

std::optional<OutlineMaskAttachments> OutlineMaskTargets::Begin(const FrameContext& frame,
                                                                const OutlineCompositeInputs& outlines)
{
    Quiet.NoteUsed();
    if (frame.ScenePost == nullptr || frame.SceneColorBindless == UINT32_MAX || frame.DepthView == VK_NULL_HANDLE)
        return std::nullopt;

    Targets.SetExtent(Mask, frame.SceneExtent);
    Targets.SetExtent(MarkedTiles, OutlineTileExtent(frame.SceneExtent));
    Targets.SetExtent(NearTiles, OutlineTileExtent(frame.SceneExtent));
    const std::optional<RenderTargetView> mask = Targets.Acquire(Mask);
    const std::optional<RenderTargetView> marked = Targets.Acquire(MarkedTiles);
    const std::optional<RenderTargetView> near = Targets.Acquire(NearTiles);
    StyleBindless = StyleImage.Update(*outlines.Styles);
    if (!mask.has_value() || !marked.has_value() || !near.has_value() || StyleBindless == UINT32_MAX)
        return std::nullopt;
    Tiles = OutlineTileTargets{ .Marked = *marked, .Near = *near };

    MaskBindless = mask->BindlessIndex;
    return OutlineMaskAttachments{ .MaskImage = mask->ColorImage,
                                   .MaskView = mask->ColorView,
                                   .MaskAccess = mask->ColorAccess,
                                   .Extent = mask->Extent,
                                   .DepthView = frame.DepthView,
                                   .DepthFormat = frame.DepthFormat,
                                   .StencilFormat = frame.StencilFormat };
}

void PublishOutlineComposite(const FrameContext& frame, ScenePostState& post, OutlineTilePass& tilePass,
                             const OutlineCompositeTargets& targets, const OutlineCompositeInputs& outlines,
                             const Mat4& projection, VkExtent2D extent)
{
    const VkRect2D scissor = OutlineScissorFor(outlines, extent);
    if (scissor.extent.width == 0 || scissor.extent.height == 0)
        return;
    const std::uint32_t tiles = tilePass.Record(frame, targets.MaskBindless, targets.Tiles, scissor,
                                                outlines.MaxWidthPixels);
    if (tiles == UINT32_MAX)
        return;
    post.OutlineMaskBindless = targets.MaskBindless;
    post.OutlineStyleBindless = targets.StyleBindless;
    post.OutlineTilesBindless = tiles;
    post.OutlineMaxWidthPixels = outlines.MaxWidthPixels;
    post.OutlineDepthTerms = OutlineDepthTermsOf(projection);
    post.OutlineScissor = scissor;
}

void OutlineMaskTargets::Publish(const FrameContext& frame, const OutlineCompositeInputs& outlines,
                                 const Mat4& projection)
{
    PublishOutlineComposite(frame, *frame.ScenePost, TilePass,
                            OutlineCompositeTargets{ .MaskBindless = MaskBindless,
                                                     .StyleBindless = StyleBindless,
                                                     .Tiles = Tiles },
                            outlines, projection, frame.SceneExtent);
}

bool OutlineMaskTargets::HoldsMaskImage()
{
    return Targets.Peek(Mask).has_value();
}
