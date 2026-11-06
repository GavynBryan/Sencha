#include <graphics/vulkan/OutlineMaskTargets.h>

#include <graphics/vulkan/VulkanSamplerCache.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
constexpr std::uint32_t kMaskQuietFrames = 120;
}

VkRect2D OutlineScissorFor(const OutlineFrame& outlines, VkExtent2D extent)
{
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
    UploadedVersion = 0;
}

std::uint32_t OutlineStyleImage::Update(const OutlineStyleTable& styles, std::uint32_t version)
{
    if (Image.IsNull())
    {
        ImageCreateInfo info;
        info.Format = VK_FORMAT_R32G32B32A32_SFLOAT;
        info.Extent = { static_cast<std::uint32_t>(styles.size()) * 2, 1 };
        info.Usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        info.DebugName = "outline_styles";
        Image = Services.Images->Create(info);
        if (Image.IsNull())
            return UINT32_MAX;
        UploadedVersion = 0;
    }
    if (UploadedVersion != version)
    {
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
        UploadedVersion = version;
        if (!Bindless.IsValid())
            Bindless = Services.Descriptors->RegisterSampledImage(Image, Services.Samplers->GetNearestClamp());
    }
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
    if (++QuietFrames != kMaskQuietFrames)
        return;
    Targets.Evict(Mask);
    Targets.Evict(MarkedTiles);
    Targets.Evict(NearTiles);
}

std::optional<OutlineMaskAttachments> OutlineMaskTargets::Begin(const FrameContext& frame, const OutlineFrame& outlines)
{
    QuietFrames = 0;
    if (frame.ScenePost == nullptr || frame.SceneColorBindless == UINT32_MAX || frame.DepthView == VK_NULL_HANDLE)
        return std::nullopt;

    Targets.SetExtent(Mask, frame.SceneExtent);
    Targets.SetExtent(MarkedTiles, OutlineTileExtent(frame.SceneExtent));
    Targets.SetExtent(NearTiles, OutlineTileExtent(frame.SceneExtent));
    const std::optional<RenderTargetView> mask = Targets.Acquire(Mask);
    const std::optional<RenderTargetView> marked = Targets.Acquire(MarkedTiles);
    const std::optional<RenderTargetView> near = Targets.Acquire(NearTiles);
    StyleBindless = StyleImage.Update(outlines.Styles, outlines.StylesVersion);
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

void OutlineMaskTargets::Publish(const FrameContext& frame, const OutlineFrame& outlines, const Mat4& projection)
{
    const VkRect2D scissor = OutlineScissorFor(outlines, frame.SceneExtent);
    const std::uint32_t tiles = TilePass.Record(frame, MaskBindless, Tiles, scissor, outlines.MaxWidthPixels);
    if (tiles == UINT32_MAX)
        return;
    frame.ScenePost->OutlineMaskBindless = MaskBindless;
    frame.ScenePost->OutlineStyleBindless = StyleBindless;
    frame.ScenePost->OutlineTilesBindless = tiles;
    frame.ScenePost->OutlineMaxWidthPixels = outlines.MaxWidthPixels;
    frame.ScenePost->OutlineDepthTerms = OutlineDepthTermsOf(projection);
    frame.ScenePost->OutlineScissor = scissor;
}

bool OutlineMaskTargets::HoldsMaskImage()
{
    return Targets.Peek(Mask).has_value();
}
